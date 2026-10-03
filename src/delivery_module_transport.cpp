#include "delivery_module_transport.h"

#include <utility>

#include <QByteArray>
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
    // unknown id is not an error — it just means "not open on this node yet",
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

std::vector<std::vector<uint8_t>> DeliveryModuleTransport::fetchHistory(
    const std::string& contentTopic, const std::string& peerAddr, const std::string& requestId,
    int64_t timeoutMs) {
    std::vector<std::vector<uint8_t>> out;

    json req;
    req["requestId"] = requestId;
    req["includeData"] = true;
    req["paginationForward"] = true;
    req["contentTopics"] = json::array({contentTopic});

    LogosResult res = modules_.delivery_module.storeQuery(qs(req.dump()), qs(peerAddr),
                                                          static_cast<int>(timeoutMs));
    if (!res.success)
        return out;

    // StoreQueryResponseHex: { "messages": [ { "message": { "payload": base64 } } ] }.
    // The Qt wrapper may hand the response back as a JSON string or as an
    // already-structured map/list; normalise both to JSON text.
    QString text = res.value.toString();
    if (text.isEmpty())
        text = QString::fromUtf8(QJsonDocument::fromVariant(res.value).toJson(QJsonDocument::Compact));

    const json response = json::parse(text.toStdString(), nullptr, false);
    if (response.is_discarded() || !response.is_object())
        return out;
    auto messages = response.find("messages");
    if (messages == response.end() || !messages->is_array())
        return out;

    for (const auto& entry : *messages) {
        if (!entry.is_object() || !entry.contains("message"))
            continue;
        const json& message = entry["message"];
        if (!message.is_object() || !message.contains("payload") || !message["payload"].is_string())
            continue;
        const std::string decoded = base64Decode(message["payload"].get<std::string>());
        out.emplace_back(decoded.begin(), decoded.end());
    }
    return out;
}