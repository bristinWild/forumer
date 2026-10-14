#include "forumer_core/digest_plan.h"

#include <algorithm>

namespace forumer::sync {

DigestPlan planDigest(const Digest& digest, int64_t nowMs, int64_t liveWindowMs, bool checkPeer,
                      bool mayScan, const RecentFn& recent, size_t maxAnswer) {
    DigestPlan plan;
    if (!mayScan)
        return plan;

    // Our own answers look at the live window only: posts older than that
    // are never offered, however far back the digest reaches. The peer
    // prompt looks twice as far back, so a peer that is behind us still
    // shows up as "holds posts we don't". One read covers both.
    const int64_t ownFrom = nowMs - liveWindowMs;
    const int64_t promptFrom = std::max<int64_t>(digest.sinceMs, nowMs - 2 * liveWindowMs);
    const int64_t from = checkPeer ? std::min(ownFrom, promptFrom) : ownFrom;

    const auto held = recent(from);
    plan.scanned = true;

    if (checkPeer) {
        std::vector<PostSummary> wide;
        for (const auto& p : held)
            if (p.timestampMs >= promptFrom)
                wide.push_back(p);
        plan.peerHoldsUnknown = listsUnknown(digest, wide);
    }

    std::vector<PostSummary> own;
    for (const auto& p : held)
        if (p.timestampMs >= ownFrom)
            own.push_back(p);
    plan.missing = missingFrom(digest, own, maxAnswer);
    return plan;
}

std::optional<Digest> RangeGate::admit(const Digest& range, int64_t nowMs, int64_t gapMs,
                                       int64_t maxSkewMs, int64_t maxSpanMs) {
    // One at a time: a stream of range requests can't make us replay history
    // faster than this.
    if (nowMs - lastAnswerMs_ < gapMs)
        return std::nullopt;

    Digest window = range;
    window.untilMs = std::min<int64_t>(window.untilMs, nowMs + maxSkewMs);
    if (window.untilMs <= window.sinceMs)
        return std::nullopt;   // not a window: refused, gap untouched
    // Overflow-safe for any decoded pair: answer only the newest part.
    if (spanExceeds(window.sinceMs, window.untilMs, maxSpanMs))
        window.sinceMs = window.untilMs - maxSpanMs;

    lastAnswerMs_ = nowMs;     // the gap starts only now
    return window;
}

} // namespace forumer::sync