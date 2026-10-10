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
//      time. Counting on the post's own timestamps keeps catch-up after a day
//      offline from looking like a flood - but timestamps are the author's
//      to choose, so on its own this lets one key back-date hundreds of posts
//      into past hours (review F6). Two more limits close that:
//
//      2a. Live posts: at most maxPerArrival() of one key's posts are
//          accepted per kWindowMs of OUR clock (twice the hourly limit, so
//          an honest catch-up still fits). More are deferred; peers offer
//          them again with later digests.
//      2b. Posts from history walks and store nodes, which arrive in bulk:
//          at most maxPerDay() per key in any 24 h of author time.
//
//   3. Brand-new keys (anonymous posts, auto-rotated personas): each is
//      fresh, so (2) can't see them. Incoming posts from keys we've never seen
//      draw from a shared budget (kNewKeyPerMinute, bursting to kNewKeyBurst).
//      A post that finds the budget empty isn't dropped for good: it is simply
//      not stored yet, and the next digest exchange offers it again.
//      Posts carrying at least kPriorityPowBits of work first draw from a
//      lane of their own (kPriorityPerMinute), so cheap fresh keys can't
//      crowd out the costlier ones (review F7). Honest clients mine a key's
//      first post at kPriorityPowBits; anonymous posts already exceed it.
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

inline constexpr int kArrivalFactor = 2;                  // 2a: arrivals per hour, x hourly limit
inline constexpr int64_t kDayMs = 24LL * 60 * 60 * 1000;
inline constexpr int kMaxTopicsPerDay = 40;               // 2b: per key / account per 24 h
inline constexpr int kMaxRepliesPerDay = 240;

inline constexpr double kNewKeyPerMinute = 60;            // posts from never-seen keys
inline constexpr double kNewKeyBurst = 120;
inline constexpr int kPriorityPowBits = 18;               // a fresh key's first post, mined by honest clients
inline constexpr double kPriorityPerMinute = 30;          // their own lane, on top of the shared one
inline constexpr double kPriorityBurst = 60;
inline constexpr double kResendPerMinute = 240;           // digest answers
inline constexpr double kResendBurst = 128;

/// The hourly limit for a kind of post.
int maxPerWindow(post::Kind kind);

/// Whether one more post fits, given how many of the same kind the author
/// (or account) already has in the window.
bool withinLimit(post::Kind kind, size_t alreadyInWindow);

/// 2a: most posts of one key accepted per kWindowMs of arrival time.
int maxPerArrival(post::Kind kind);
/// 2b: most posts of one key (or account) in any kDayMs of author time.
int maxPerDay(post::Kind kind);

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