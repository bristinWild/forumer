#pragma once

// What one unsigned sync message may make a node do (0.2.4, Phase A).
//
// Digests and range requests arrive from any peer, unsigned, so the work they
// can trigger must be bounded by the node's own rules, not by the sender:
//
//   - A digest costs at most ONE read of the held set (planDigest). The old
//     handler read it two or three times per digest.
//   - Digests draw from a shared scan budget (flood::kDigestScansPerMinute).
//     When it is empty the digest is not evaluated at all; the next periodic
//     digest carries the same information, the same way new-key posts are
//     deferred rather than dropped.
//   - A range request is answered only if it is a valid window, and only then
//     does the one-at-a-time gap start (RangeGate). An invalid request used to
//     start the gap anyway, so junk could silence real history requests.
//
// Pure logic: no Qt, no storage module, so tests can count reads.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "forumer_core/sync.h"

namespace forumer::sync {

/// Reads the held posts dated at or after `sinceMs`, newest first
/// (PostStore::recent for the forum in question).
using RecentFn = std::function<std::vector<PostSummary>(int64_t sinceMs)>;

struct DigestPlan {
    bool scanned = false;              // whether the held set was read at all
    bool peerHoldsUnknown = false;     // the sender lists posts we don't hold: send our digest back
    std::vector<std::string> missing;  // our live-window posts the sender lacks, newest first, capped
};

/// Plans the answer to one live digest with at most one call to `recent`.
///   checkPeer: the peer-prompt check is due (its own gap has passed).
///   mayScan:   false when the scan budget is spent; then nothing is read.
DigestPlan planDigest(const Digest& digest, int64_t nowMs, int64_t liveWindowMs, bool checkPeer,
                      bool mayScan, const RecentFn& recent, size_t maxAnswer = kMaxAnswer);

/// Admits range requests one at a time. The gap starts only when something is
/// actually sent: admit() alone never starts it, so a valid window with nothing
/// missing (or a send that was dropped) doesn't silence the next request.
class RangeGate {
public:
    /// The window to answer for this request, clamped to the clock skew and
    /// span limits, or nullopt if it is refused (too soon, or not a window).
    /// Does not start the gap; call answered() once an answer goes out.
    std::optional<Digest> admit(const Digest& range, int64_t nowMs, int64_t gapMs,
                                int64_t maxSkewMs, int64_t maxSpanMs);

    /// Starts the gap: call only after an answer was actually queued.
    void answered(int64_t nowMs) { lastAnswerMs_ = nowMs; }

private:
    int64_t lastAnswerMs_ = 0;
};

} // namespace forumer::sync