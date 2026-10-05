#include "delivery_module_transport.h"

#include <utility>

#include <QByteArray>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QString>
#include <QVariant>

#include <nlohmann/json.hpp>

#include "base64.h"
#include "logos_sdk.h"
#include "logos_types.h"

using json = nlohmann::json;

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }

QByteArray qba(const std::vector<uint8_t>& bytes) {
    return QByteArray(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<qsizetype>(bytes.size()));
}

// LogosResult::getError() throws on a success and getString() throws on a
// failure, so never reach for either without checking .success first.
std::string errorOf(const LogosResult& r) {
    return r.success ? std::string() : r.getError().toStdString();
}

std::string stringOf(const LogosResult& r) {
    return r.success ? r.getString().toStdString() : std::string();
}

} // namespace

DeliveryModuleTransport::DeliveryModuleTransport(LogosModules& modules, std::string peerId,
                                                 bool useChannels)
    : modules_(modules), peerId_(std::move(peerId)), channelsAvailable_(useChannels) {}

bool DeliveryModuleTransport::ensureChannel(const std::string& topic) {
    if (!channelsAvailable_)
        return false;
    if (channels_.count(topic) != 0)
        return true;

    // channelExists() answers with the literal string "true"/"false", and an
    // unknown id is not an error - it just means "not open on this node yet",
    // including after a restart, where channelCreate() re-opens the persisted
    // channel state rather than starting over.
    LogosResult exists = modules_.delivery_module.channelExists(qs(topic));
    const bool open = exists.success && stringOf(exists) == "true";
    if (!open && !modules_.delivery_module.channelCreate(qs(topic), qs(topic), qs(peerId_)).success) {
        // A kernel-only node (or one another app configured without the
        // channels layer) fails every channel call. Stop asking.
        channelsAvailable_ = false;
        return false;
    }
    channels_.insert(topic);
    return true;
}

std::string DeliveryModuleTransport::join(const std::string& contentTopic) {
    if (ensureChannel(contentTopic))
        return {};  // a channel receives as soon as it is open
    LogosResult r = modules_.delivery_module.subscribe(qs(contentTopic));
    return r.success ? std::string() : errorOf(r);
}

DeliveryModuleTransport::SendResult DeliveryModuleTransport::publish(
    const std::string& contentTopic, const std::vector<uint8_t>& payload) {
    LogosResult r = ensureChannel(contentTopic)
                        ? modules_.delivery_module.channelSend(qs(contentTopic), qba(payload))
                        : modules_.delivery_module.send(qs(contentTopic), qba(payload));
    if (!r.success)
        return {false, {}, errorOf(r)};
    return {true, stringOf(r), {}};
}

std::string DeliveryModuleTransport::autoshardPubsubTopic(const std::string& contentTopic,
                                                          int cluster, int shards) {
    // "/application/version/name/encoding"
    std::vector<std::string> parts;
    size_t i = 1;
    while (i <= contentTopic.size()) {
        const size_t j = contentTopic.find('/', i);
        parts.push_back(contentTopic.substr(i, j == std::string::npos ? std::string::npos : j - i));
        if (j == std::string::npos)
            break;
        i = j + 1;
    }
    if (contentTopic.empty() || contentTopic[0] != '/' || parts.size() < 2 || shards <= 0)
        return {};
    const QByteArray digest = QCryptographicHash::hash(
        QByteArray::fromStdString(parts[0] + parts[1]), QCryptographicHash::Sha256);
    uint64_t v = 0;
    for (int k = 24; k < 32; ++k)
        v = (v << 8) | static_cast<uint8_t>(digest[k]);
    return "/waku/2/rs/" + std::to_string(cluster) + "/" +
           std::to_string(v % static_cast<uint64_t>(shards));
}

DeliveryModuleTransport::StorePage DeliveryModuleTransport::parseStorePage(
    const std::string& responseJson) {
    StorePage page;
    const json response = json::parse(responseJson, nullptr, false);
    if (response.is_discarded() || !response.is_object()) {
        page.error = "unreadable store response";
        return page;
    }
    const auto code = response.find("statusCode");
    if (code != response.end() && code->is_number_integer()) {
        const int c = code->get<int>();
        if (c < 200 || c >= 300) {
            const auto desc = response.find("statusDesc");
            page.error = "store node said " + std::to_string(c) +
                         (desc != response.end() && desc->is_string() ? " " + desc->get<std::string>()
                                                                     : std::string());
            return page;
        }
    }
    const auto messages = response.find("messages");
    if (messages == response.end() || !messages->is_array()) {
        page.error = "store response without messages";
        return page;
    }
    for (const auto& entry : *messages) {
        if (!entry.is_object())
            continue;
        const auto message = entry.find("message");
        if (message == entry.end() || !message->is_object())
            continue;
        const auto payload = message->find("payload");
        if (payload == message->end() || !payload->is_string())
            continue;
        const std::string decoded = base64Decode(payload->get<std::string>());
        if (!decoded.empty())
            page.payloads.emplace_back(decoded.begin(), decoded.end());
    }
    const auto cursor = response.find("paginationCursor");
    if (cursor != response.end() && cursor->is_string())
        page.cursor = cursor->get<std::string>();
    page.ok = true;
    return page;
}

void DeliveryModuleTransport::queryStorePage(const std::string& contentTopic, int cluster,
                                             int64_t sinceMs, const std::string& cursor,
                                             const std::string& peerAddr, int64_t timeoutMs,
                                             std::function<void(StorePage)> done) {
    static uint64_t counter = 0;
    json req;
    req["requestId"] = "forumer-" + std::to_string(++counter);
    req["includeData"] = true;
    req["paginationForward"] = true;
    req["paginationLimit"] = 100;
    req["pubsubTopic"] = autoshardPubsubTopic(contentTopic, cluster);
    req["contentTopics"] = json::array({contentTopic});
    req["timeStart"] = sinceMs * 1'000'000;  // nanoseconds
    if (!cursor.empty())
        req["paginationCursor"] = cursor;

    // Async: a synchronous storeQuery would block this thread (and the view's
    // replica) for as long as the store node takes to answer.
    modules_.delivery_module.storeQueryAsync(
        qs(req.dump()), qs(peerAddr), timeoutMs, [done = std::move(done)](LogosResult r) {
            if (!r.success) {
                StorePage failed;
                failed.error = errorOf(r);
                if (failed.error.empty())
                    failed.error = "no answer";
                done(std::move(failed));
                return;
            }
            // The Qt wrapper may hand the response back as JSON text or as an
            // already-structured map; normalise both to text.
            QString text = r.value.toString();
            if (text.isEmpty())
                text = QString::fromUtf8(QJsonDocument::fromVariant(r.value).toJson(QJsonDocument::Compact));
            done(parseStorePage(text.toStdString()));
        });
}