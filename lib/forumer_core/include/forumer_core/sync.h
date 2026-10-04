#pragma once

// The sync protocol: what Forumer puts on the wire, and the rules for filling
// gaps between peers.
//
// Each forum is ONE content topic (one Logos Delivery reliable channel):
//
//     /forumer/3/<forum>/proto            e.g. /forumer/3/public/proto
//
// carrying two kinds of message, both small JSON objects:
//
//   post    {"v":1, "t":"post",   "env":"<signed envelope JSON>"}
//           One signed post or reply (see post.h). Receivers verify it and
//           store it once; repeats are harmless.
//
//   digest  {"v":1, "t":"digest", "since":<ms>, "have":["<short id>", ...]}
//           "These are the posts I hold from `since` onwards." Every peer that
//           holds a post in that window which is NOT listed answers by sending
//           that post again. Sent shortly after joining, then periodically, and
//           on demand ("Catch up").
//
// Digests are what make delivery dependable. A post can be lost on the way in
// (the receiver had only just joined, was offline, or the network dropped
// it); the next digest from the receiver names what it holds, and whoever
// has the missing post re-sends it. No server is involved and nobody needs to
// be online at the same time as the author - only someone who has the post.
//
// Everything here is pure (no I/O), so the rules are unit-tested.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "forumer_core/post.h"
#include "forumer_core/post_store.h"

namespace forumer::sync {

inline constexpr int kWireVersion = 1;
inline constexpr int kTopicVersion = 3;  // LIP-23 version segment of the content topic

/// Largest payload we accept. A post envelope is capped at post::kMaxEncoded;
/// a full digest is about 10 KB.
inline constexpr size_t kMaxPayload = 24 * 1024;

/// Digests list ids shortened to this many hex chars (64 bits): plenty to
/// tell a forum's posts apart, and a quarter of the size of a full id.
inline constexpr size_t kShortIdLength = 16;

/// Most ids one digest lists. When a peer holds more, the digest covers only
/// the newest kMaxDigestIds and moves `since` forward to match.
inline constexpr size_t kMaxDigestIds = 512;

/// Most posts one peer re-sends in answer to one digest.
inline constexpr size_t kMaxAnswer = 64;

/// How far back digests reach (and therefore how long gaps stay repairable
/// without a store node): 48 hours.
inline constexpr int64_t kWindowMs = 48LL * 60 * 60 * 1000;

/// Posts dated further in the future than this are refused (a post can't
/// pin itself to the top of every feed by lying about its time).
inline constexpr int64_t kMaxClockSkewMs = 10LL * 60 * 1000;

/// The content topic of a forum. Forum names are [a-z0-9-], 1..64 chars;
/// returns "" for anything else.
std::string contentTopic(std::string_view forum);

//  Wire messages 

struct Digest {
    int64_t sinceMs = 0;
    std::vector<std::string> have;  // short ids (see shortId)
};

enum class MessageType { Post, Digest };

struct Message {
    MessageType type = MessageType::Post;
    std::optional<post::Post> post;  // Post: parsed but NOT yet verified
    Digest digest;                   // Digest
};

enum class DecodeError {
    None,
    TooLarge,            // over kMaxPayload
    Malformed,           // not one of our messages
    UnsupportedVersion,  // a newer wire version than this build speaks
};

const char* describe(DecodeError error);

struct DecodeResult {
    std::optional<Message> message;
    DecodeError error = DecodeError::None;
};

std::vector<uint8_t> encodePost(const post::Post& post);
std::vector<uint8_t> encodeDigest(const Digest& digest);

/// Parse a payload. Checks shape and size only: a decoded post must still go
/// through post::verify() and acceptTimestamp() before it is trusted.
DecodeResult decode(const std::vector<uint8_t>& payload);

// ── Gap repair ───────────────────────────────────────────────────────────────

/// The first kShortIdLength chars of a post id.
std::string shortId(std::string_view id);

/// Build the digest for what we hold. `held` is newest first (as
/// PostStore::recent returns it). If there are more than `maxIds`, only the
/// newest are listed and `since` moves up to the oldest listed post, so the
/// digest never claims we lack something we simply left out.
Digest makeDigest(const std::vector<PostSummary>& held, int64_t sinceMs,
                  size_t maxIds = kMaxDigestIds);

/// Which of our posts the digest's sender is missing: those in `held`
/// (newest first) dated at or after its `since` and not listed in it. At
/// most `max`, newest first - full post ids.
std::vector<std::string> missingFrom(const Digest& digest, const std::vector<PostSummary>& held,
                                     size_t max = kMaxAnswer);

/// Whether the digest lists any post not in `held` - i.e. its sender has
/// something we lack. We then send our own digest promptly so the sender
/// answers it, instead of waiting for our next periodic one. Pass `held` for
/// the digest's whole window (PostStore::recent(digest.sinceMs)), or posts we
/// hold but dated before our own window would look unknown.
bool listsUnknown(const Digest& digest, const std::vector<PostSummary>& held);

/// Whether a post's author timestamp is plausible relative to our clock.
bool acceptTimestamp(int64_t postMs, int64_t nowMs);

//  Outbox retries 

/// How long to wait after attempt number `attempts` before the next one:
/// 15 s, 30 s, 1 min, 2 min, 4 min, then every 5 min.
int64_t retryDelayMs(int attempts);

/// Whether an unsent outbox row should be sent again now. Never-attempted
/// rows are always due; Sent rows never are.
bool dueForRetry(const OutboxEntry& entry, int64_t nowMs);

} // namespace forumer::sync