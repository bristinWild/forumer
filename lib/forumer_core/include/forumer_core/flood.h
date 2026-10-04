#pragma once

// Flood control: how much any one participant can make everyone else carry.
//
// Forumer has no server to throttle anyone, so every peer applies the same
// rules to what it accepts, and every client applies them to what it sends:
//
//   1. Proof-of-work on every post (post.h): 16 bits signed, 20 bits
//      anonymous. A cost per post, paid by the author's CPU.
//
//   2. Per persona (verifiable by anyone): at most kMaxTopics topics and
//      kMaxReplies replies signed by one key within any kWindowMs of AUTHOR
//      time. Counting on the post's own timestamps rather than arrival time
//      keeps catch-up after a day offline from looking like a flood.
//
//   3. Brand-new keys (anonymous posts, auto-rotated personas): each is
//      fresh, so (2) can't see them. Incoming posts from keys we've never seen
//      draw from a shared budget (kNewKeyPerMinute, bursting to kNewKeyBurst).
//      A post that finds the budget empty isn't dropped for good: it is simply
//      not stored yet, and the next digest exchange offers it again.
//
//   4. Per account, on the sender (honest clients): the same hourly limits,
//      counted over everything the account posted from this device - so
//      rotating personas or posting anonymously doesn't lift them.
//
//   5. Re-sending to repair other peers' gaps is capped (kResendPerMinute), so
//      a stream of digests can't turn a peer into an amplifier.
//
// Sizes are capped separately (post::kMaxEncoded per post, sync::kMaxPayload
// per message). Rate-Limiting Nullifiers (RLN) in Logos Delivery v0.3 would
// replace (3) with a cryptographic per-member limit; see the README.

#include <cstdint>
#include <vector>

#include "forumer_core/post.h"

namespace forumer::flood {

inline constexpr int64_t kWindowMs = 60LL * 60 * 1000;   // one hour
inline constexpr int kMaxTopics = 10;                     // per persona / account per window
inline constexpr int kMaxReplies = 60;

inline constexpr double kNewKeyPerMinute = 60;            // posts from never-seen keys
inline constexpr double kNewKeyBurst = 120;
inline constexpr double kResendPerMinute = 240;           // digest answers
inline constexpr double kResendBurst = 128;

/// The hourly limit for a kind of post.
int maxPerWindow(post::Kind kind);

/// Whether one more post fits, given how many of the same kind the author
/// (or account) already has in the window.
bool withinLimit(post::Kind kind, size_t alreadyInWindow);

/// How long until a post of `kind` fits again, given the author timestamps of
/// the posts in the current window (any order). 0 if it fits now.
int64_t waitMs(post::Kind kind, const std::vector<int64_t>& timestampsInWindow, int64_t nowMs);

/// A classic token bucket: `ratePerMinute` tokens a minute, at most `burst`
/// saved up. Starts full.
class TokenBucket {
public:
    TokenBucket(double ratePerMinute, double burst);

    /// Take one token if there is one.
    bool take(int64_t nowMs);

    /// Tokens available now (for logs and tests).
    double available(int64_t nowMs);

private:
    void refill(int64_t nowMs);

    double rate_;       // tokens per ms
    double burst_;
    double tokens_;
    int64_t lastMs_ = -1;
};

} // namespace forumer::flood