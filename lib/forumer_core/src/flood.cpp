#include "forumer_core/flood.h"

#include <algorithm>

namespace forumer::flood {

int maxPerWindow(post::Kind kind) {
    return kind == post::Kind::Post ? kMaxTopics : kMaxReplies;
}

bool withinLimit(post::Kind kind, size_t alreadyInWindow) {
    return alreadyInWindow < static_cast<size_t>(maxPerWindow(kind));
}

int maxPerArrival(post::Kind kind) { return kArrivalFactor * maxPerWindow(kind); }

int maxPerDay(post::Kind kind) {
    return kind == post::Kind::Post ? kMaxTopicsPerDay : kMaxRepliesPerDay;
}

int64_t waitMs(post::Kind kind, const std::vector<int64_t>& timestampsInWindow, int64_t nowMs) {
    const size_t limit = static_cast<size_t>(maxPerWindow(kind));
    if (timestampsInWindow.size() < limit) return 0;
    // The window must shed (count - limit + 1) posts: wait until the oldest of
    // those leaves it.
    std::vector<int64_t> ts = timestampsInWindow;
    std::sort(ts.begin(), ts.end());
    const int64_t leaves = ts[ts.size() - limit] + kWindowMs;
    return std::max<int64_t>(0, leaves - nowMs + 1);
}

TokenBucket::TokenBucket(double ratePerMinute, double burst)
    : rate_(ratePerMinute / 60000.0), burst_(burst), tokens_(burst) {}

void TokenBucket::refill(int64_t nowMs) {
    if (lastMs_ >= 0 && nowMs > lastMs_)
        tokens_ = std::min(burst_, tokens_ + (nowMs - lastMs_) * rate_);
    if (lastMs_ < 0 || nowMs > lastMs_)
        lastMs_ = nowMs;
}

bool TokenBucket::take(int64_t nowMs) {
    refill(nowMs);
    if (tokens_ < 1.0) return false;
    tokens_ -= 1.0;
    return true;
}

double TokenBucket::available(int64_t nowMs) {
    refill(nowMs);
    return tokens_;
}

} // namespace forumer::flood