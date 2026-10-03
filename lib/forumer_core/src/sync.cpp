#include "forumer_core/sync.h"

#include <algorithm>
#include <unordered_set>

#include <nlohmann/json.hpp>

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
    return toBytes({{"v", kWireVersion}, {"t", "digest"}, {"since", digest.sinceMs}, {"have", digest.have}});
}

DecodeResult decode(const std::vector<uint8_t>& payload) {
    if (payload.size() > kMaxPayload)
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

    if (type == "digest") {
        auto since = j.find("since");
        auto have = j.find("have");
        if (since == j.end() || !since->is_number_integer() || have == j.end() || !have->is_array())
            return fail(DecodeError::Malformed);
        if (have->size() > kMaxDigestIds)
            return fail(DecodeError::Malformed);
        msg.type = MessageType::Digest;
        msg.digest.sinceMs = since->get<int64_t>();
        msg.digest.have.reserve(have->size());
        for (const auto& id : *have) {
            if (!id.is_string() || !isShortId(id.get<std::string>()))
                return fail(DecodeError::Malformed);
            msg.digest.have.push_back(id.get<std::string>());
        }
        return {std::move(msg), DecodeError::None};
    }

    return fail(DecodeError::Malformed);
}

//  Gap repair 

std::string shortId(std::string_view id) {
    return std::string(id.substr(0, std::min(id.size(), kShortIdLength)));
}

Digest makeDigest(const std::vector<PostSummary>& held, int64_t sinceMs, size_t maxIds) {
    Digest d;
    d.sinceMs = sinceMs;
    int64_t oldestListed = sinceMs;
    bool truncated = false;
    for (const auto& p : held) {
        if (p.timestampMs < sinceMs)
            continue;
        if (d.have.size() == maxIds) {
            truncated = true;
            break;
        }
        d.have.push_back(shortId(p.id));
        oldestListed = p.timestampMs;
    }
    // Out of room: claim only the window we listed in full. A post sharing the
    // cut-off timestamp but left out may get re-sent to us — harmless.
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
        if (p.timestampMs < digest.sinceMs)
            continue;
        if (have.count(shortId(p.id)) == 0)
            out.push_back(p.id);
    }
    return out;
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