#include "forumer_core/sync.h"

#include <algorithm>
#include <unordered_set>

#include <nlohmann/json.hpp>

#include "forumer_core/media.h"

namespace forumer::sync {

using json = nlohmann::json;

namespace {

bool isForumChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
}

bool isShortId(const std::string& s) {
    if (s.size() != kShortIdLength)
        return false;
    return std::all_of(s.begin(), s.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}

std::vector<uint8_t> toBytes(const json& j) {
    const std::string text = j.dump();
    return std::vector<uint8_t>(text.begin(), text.end());
}

DecodeResult fail(DecodeError e) { return {std::nullopt, e}; }

} // namespace

std::string contentTopic(std::string_view forum) {
    if (forum.empty() || forum.size() > 64 || !std::all_of(forum.begin(), forum.end(), isForumChar))
        return {};
    return "/forumer/" + std::to_string(kTopicVersion) + "/" + std::string(forum) + "/proto";
}

const char* describe(DecodeError error) {
    switch (error) {
    case DecodeError::None:               return "ok";
    case DecodeError::TooLarge:           return "message too large";
    case DecodeError::Malformed:          return "not a Forumer message";
    case DecodeError::UnsupportedVersion: return "newer wire version";
    }
    return "unknown";
}

//  Wire messages 

std::vector<uint8_t> encodePost(const post::Post& post) {
    return toBytes({{"v", kWireVersion}, {"t", "post"}, {"env", post.toJson()}});
}

std::vector<uint8_t> encodeDigest(const Digest& digest) {
    json j{{"v", kWireVersion}, {"t", "digest"}, {"since", digest.sinceMs}, {"have", digest.have}};
    if (digest.oldestMs > 0)
        j["oldest"] = digest.oldestMs;
    return toBytes(j);
}

std::vector<uint8_t> encodeRange(const Digest& range) {
    return toBytes({{"v", kWireVersion}, {"t", "range"}, {"since", range.sinceMs},
                    {"until", range.untilMs}, {"have", range.have}});
}

std::vector<uint8_t> encodeMediaAsk(const std::string& cid) {
    return toBytes({{"v", kWireVersion}, {"t", "mask"}, {"cid", cid}});
}

std::vector<uint8_t> encodeMediaHere(const MediaHint& hint) {
    return toBytes({{"v", kWireVersion}, {"t", "mhere"}, {"cid", hint.cid}, {"peer", hint.peer},
                    {"addrs", hint.addrs}});
}

namespace {

// A libp2p peer id as text: base58, 40..64 characters.
bool peerIdShape(const std::string& s) {
    if (s.size() < 40 || s.size() > 64) return false;
    for (char c : s)
        if (!((c >= '1' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) || c == 'I' ||
            c == 'O' || c == 'l')
            return false;
    return true;
}

// A multiaddr we are willing to dial: /ip4|ip6|dns4|dns6/<host>/tcp|udp/<port>[/...],
// printable, short.
bool addrShape(const std::string& s) {
    if (s.size() < 8 || s.size() > 128) return false;
    if (s.rfind("/ip4/", 0) != 0 && s.rfind("/ip6/", 0) != 0 && s.rfind("/dns4/", 0) != 0 &&
        s.rfind("/dns6/", 0) != 0)
        return false;
    if (s.find("/tcp/") == std::string::npos && s.find("/udp/") == std::string::npos) return false;
    for (char c : s)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '/' ||
              c == '.' || c == ':' || c == '-' || c == '_'))
            return false;
    return true;
}

} // namespace

std::vector<std::vector<uint8_t>> encodePreview(const std::string& postId, uint32_t index,
                                                const Bytes& blob, const Bytes& signature) {
    std::vector<std::vector<uint8_t>> out;
    const size_t parts = (blob.size() + kPreviewPartBytes - 1) / kPreviewPartBytes;
    if (parts == 0 || parts > kMaxPreviewParts) return out;
    for (size_t k = 0; k < parts; ++k) {
        const size_t from = k * kPreviewPartBytes;
        const size_t len = std::min(kPreviewPartBytes, blob.size() - from);
        out.push_back(toBytes({{"v", kWireVersion}, {"t", "mprev"}, {"post", postId}, {"i", index},
                               {"k", k}, {"n", parts}, {"sig", toBase64Url(signature)},
                               {"data", toBase64Url(blob.data() + from, len)}}));
    }
    return out;
}

DecodeResult decode(const std::vector<uint8_t>& payload) {
    if (payload.size() > kMaxPreviewPayload)
        return fail(DecodeError::TooLarge);
    // Only a preview piece may be larger than kMaxPayload: don't even parse
    // anything else that big.
    static constexpr std::string_view kPreviewTag = "\"mprev\"";
    if (payload.size() > kMaxPayload &&
        std::search(payload.begin(), payload.end(), kPreviewTag.begin(), kPreviewTag.end()) == payload.end())
        return fail(DecodeError::TooLarge);

    const json j = json::parse(payload.begin(), payload.end(), nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object())
        return fail(DecodeError::Malformed);

    auto v = j.find("v");
    auto t = j.find("t");
    if (v == j.end() || !v->is_number_integer() || t == j.end() || !t->is_string())
        return fail(DecodeError::Malformed);
    if (v->get<int64_t>() > kWireVersion)
        return fail(DecodeError::UnsupportedVersion);
    if (v->get<int64_t>() < 1)
        return fail(DecodeError::Malformed);

    const std::string type = t->get<std::string>();
    Message msg;
    if (type != "mprev" && payload.size() > kMaxPayload)
        return fail(DecodeError::TooLarge);

    if (type == "mprev") {
        auto pid = j.find("post");
        auto i = j.find("i");
        auto k = j.find("k");
        auto n = j.find("n");
        auto sig = j.find("sig");
        auto data = j.find("data");
        if (pid == j.end() || !pid->is_string() || i == j.end() || !i->is_number_unsigned() ||
            k == j.end() || !k->is_number_unsigned() || n == j.end() || !n->is_number_unsigned() ||
            sig == j.end() || !sig->is_string() || data == j.end() || !data->is_string())
            return fail(DecodeError::Malformed);
        const std::string id = pid->get<std::string>();
        if (id.size() != 32 || !std::all_of(id.begin(), id.end(), [](char c) {
                return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
            }))
            return fail(DecodeError::Malformed);
        const uint64_t index = i->get<uint64_t>(), part = k->get<uint64_t>(), parts = n->get<uint64_t>();
        if (index >= post::kMaxMedia || parts == 0 || parts > kMaxPreviewParts || part >= parts)
            return fail(DecodeError::Malformed);
        auto sigBytes = fromBase64Url(sig->get<std::string>());
        auto bytes = fromBase64Url(data->get<std::string>());
        if (!sigBytes || sigBytes->size() != 64 || !bytes || bytes->empty() || bytes->size() > kPreviewPartBytes)
            return fail(DecodeError::Malformed);
        msg.type = MessageType::Preview;
        msg.preview = {id, static_cast<uint32_t>(index), static_cast<uint32_t>(part),
                       static_cast<uint32_t>(parts), std::move(*sigBytes), std::move(*bytes)};
        return {std::move(msg), DecodeError::None};
    }

    if (type == "post") {
        auto env = j.find("env");
        if (env == j.end() || !env->is_string())
            return fail(DecodeError::Malformed);
        msg.type = MessageType::Post;
        msg.post = post::Post::fromJson(env->get<std::string>());
        if (!msg.post)
            return fail(DecodeError::Malformed);
        return {std::move(msg), DecodeError::None};
    }

    if (type == "digest" || type == "range") {
        auto since = j.find("since");
        auto have = j.find("have");
        if (since == j.end() || !since->is_number_integer() || have == j.end() || !have->is_array())
            return fail(DecodeError::Malformed);
        if (have->size() > kMaxDigestIds)
            return fail(DecodeError::Malformed);
        msg.type = type == "range" ? MessageType::Range : MessageType::Digest;
        // Times are ms since 1970: never negative. (A negative `since` with a
        // huge `until` used to overflow the span check and skip its cap.)
        if (since->get<int64_t>() < 0)
            return fail(DecodeError::Malformed);
        msg.digest.sinceMs = since->get<int64_t>();
        if (msg.type == MessageType::Range) {
            auto until = j.find("until");
            if (until == j.end() || !until->is_number_integer() ||
                until->get<int64_t>() <= msg.digest.sinceMs)
                return fail(DecodeError::Malformed);
            msg.digest.untilMs = until->get<int64_t>();
        } else if (auto oldest = j.find("oldest"); oldest != j.end()) {
            if (!oldest->is_number_integer() || oldest->get<int64_t>() < 0)
                return fail(DecodeError::Malformed);
            // Older than any Forumer post: not believable, treat as unknown.
            msg.digest.oldestMs = oldest->get<int64_t>() >= kForumEpochMs ? oldest->get<int64_t>() : 0;
        }
        msg.digest.have.reserve(have->size());
        for (const auto& id : *have) {
            if (!id.is_string() || !isShortId(id.get<std::string>()))
                return fail(DecodeError::Malformed);
            msg.digest.have.push_back(id.get<std::string>());
        }
        return {std::move(msg), DecodeError::None};
    }

    if (type == "mask" || type == "mhere") {
        auto cid = j.find("cid");
        if (cid == j.end() || !cid->is_string() || !media::cidShape(cid->get<std::string>()))
            return fail(DecodeError::Malformed);
        msg.media.cid = cid->get<std::string>();
        if (type == "mask") {
            msg.type = MessageType::MediaAsk;
            return {std::move(msg), DecodeError::None};
        }
        auto peer = j.find("peer");
        auto addrs = j.find("addrs");
        if (peer == j.end() || !peer->is_string() || !peerIdShape(peer->get<std::string>()) ||
            addrs == j.end() || !addrs->is_array() || addrs->empty() || addrs->size() > kMaxHintAddrs)
            return fail(DecodeError::Malformed);
        msg.media.peer = peer->get<std::string>();
        for (const auto& a : *addrs) {
            if (!a.is_string() || !addrShape(a.get<std::string>()))
                return fail(DecodeError::Malformed);
            msg.media.addrs.push_back(a.get<std::string>());
        }
        msg.type = MessageType::MediaHere;
        return {std::move(msg), DecodeError::None};
    }

    return fail(DecodeError::Malformed);
}

//  Gap repair 

std::string shortId(std::string_view id) {
    return std::string(id.substr(0, std::min(id.size(), kShortIdLength)));
}

Digest makeDigest(const std::vector<PostSummary>& held, int64_t sinceMs, size_t maxIds,
                  int64_t untilMs) {
    Digest d;
    d.sinceMs = sinceMs;
    d.untilMs = untilMs;
    int64_t oldestListed = sinceMs;
    bool truncated = false;
    for (const auto& p : held) {
        if (p.timestampMs < sinceMs || (untilMs > 0 && p.timestampMs >= untilMs))
            continue;
        if (d.have.size() == maxIds) {
            truncated = true;
            break;
        }
        d.have.push_back(shortId(p.id));
        oldestListed = p.timestampMs;
    }
    // Out of room: claim only the window we listed in full. A post sharing the
    // cut-off timestamp but left out may get re-sent to us - harmless.
    if (truncated)
        d.sinceMs = std::max(sinceMs, oldestListed);
    return d;
}

std::vector<std::string> missingFrom(const Digest& digest, const std::vector<PostSummary>& held,
                                     size_t max) {
    const std::unordered_set<std::string> have(digest.have.begin(), digest.have.end());
    std::vector<std::string> out;
    for (const auto& p : held) {
        if (out.size() == max)
            break;
        if (p.timestampMs < digest.sinceMs ||
            (digest.untilMs > 0 && p.timestampMs >= digest.untilMs))
            continue;
        if (have.count(shortId(p.id)) == 0)
            out.push_back(p.id);
    }
    return out;
}

bool listsUnknown(const Digest& digest, const std::vector<PostSummary>& held) {
    std::unordered_set<std::string> mine;
    mine.reserve(held.size());
    for (const auto& p : held)
        mine.insert(shortId(p.id));
    for (const auto& id : digest.have)
        if (mine.count(id) == 0)
            return true;
    return false;
}

bool spanExceeds(int64_t sinceMs, int64_t untilMs, int64_t maxSpanMs) {
    if (untilMs <= sinceMs) return false;
    // The difference of two int64s always fits in a uint64.
    const uint64_t span = static_cast<uint64_t>(untilMs) - static_cast<uint64_t>(sinceMs);
    return maxSpanMs < 0 || span > static_cast<uint64_t>(maxSpanMs);
}

bool acceptTimestamp(int64_t postMs, int64_t nowMs) {
    return postMs > 0 && postMs <= nowMs + kMaxClockSkewMs;
}

//  Outbox retries 

int64_t retryDelayMs(int attempts) {
    constexpr int64_t kBase = 15'000;
    constexpr int64_t kCap = 5 * 60'000;
    if (attempts <= 1)
        return kBase;
    const int shift = std::min(attempts - 1, 10);
    return std::min(kBase << shift, kCap);
}

bool dueForRetry(const OutboxEntry& entry, int64_t nowMs) {
    if (entry.state == SendState::Sent)
        return false;
    if (entry.lastAttemptMs == 0)
        return true;
    return nowMs - entry.lastAttemptMs >= retryDelayMs(entry.attempts);
}

} // namespace forumer::sync