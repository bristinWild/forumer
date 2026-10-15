// Phase A of 0.2.4: what unsigned sync traffic may make a node do.
//
//   A1  a digest reads the held set once, and draws on a scan budget
//   A2  a range request starts the one-at-a-time gap only when it is answered
//   A3  image-discovery lookups (mask) draw on their own budget
//   A5  log lines carry shortened identity references

#include "test.h"

#include <cstdio>
#include <string>
#include <vector>

#include "forumer_core/digest_plan.h"
#include "forumer_core/flood.h"
#include "forumer_core/redact.h"
#include "forumer_core/sync.h"

using namespace forumer;
using namespace forumer::sync;

namespace {

constexpr int64_t kH = 3'600'000;        // one hour
constexpr int64_t kNow = 10 * kH;
constexpr int64_t kLive = kH;            // live window

std::string mkId(int i) {
    char b[40];
    std::snprintf(b, sizeof b, "post%012d", i);
    return b;
}

// Records every read it is asked for, and answers from `all`.
struct FakeStore {
    std::vector<PostSummary> all;   // any order; the real store returns newest first
    int reads = 0;
    int64_t lastFrom = 0;

    RecentFn fn() {
        return [this](int64_t from) {
            ++reads;
            lastFrom = from;
            std::vector<PostSummary> out;
            for (const auto& p : all)
                if (p.timestampMs >= from)
                    out.push_back(p);
            return out;
        };
    }
};

// A post in our live window, one in the wider prompt window only.
FakeStore storeWithTwoPosts() {
    FakeStore s;
    s.all.push_back({mkId(1), kNow - kH / 2});          // 9.5h: live window
    s.all.push_back({mkId(2), kNow - kH - kH / 2});     // 11.5h ago: outside live, inside 2x
    return s;
}

Digest digestHaving(std::vector<std::string> ids, int64_t sinceMs = 0) {
    Digest d;
    d.sinceMs = sinceMs;
    for (const auto& id : ids)
        d.have.push_back(shortId(id));
    return d;
}

} // namespace

//  A1: digests 

TEST(phase_a_digest_reads_held_set_once_when_prompting) {
    FakeStore s = storeWithTwoPosts();
    // The sender lists an id we don't hold, so the peer prompt fires.
    const Digest d = digestHaving({"unknownpeerpost0"}, 0);
    const DigestPlan plan = planDigest(d, kNow, kLive, /*checkPeer=*/true, /*mayScan=*/true, s.fn());
    CHECK_EQ(s.reads, 1);
    CHECK(plan.scanned);
    CHECK(plan.peerHoldsUnknown);
    // The prompt window (2x live) was read in the same single pass.
    CHECK_EQ(s.lastFrom, kNow - 2 * kLive);
}

TEST(phase_a_digest_reads_held_set_once_without_prompt) {
    FakeStore s = storeWithTwoPosts();
    const Digest d = digestHaving({}, 0);
    const DigestPlan plan = planDigest(d, kNow, kLive, /*checkPeer=*/false, /*mayScan=*/true, s.fn());
    CHECK_EQ(s.reads, 1);
    CHECK(!plan.peerHoldsUnknown);
    CHECK_EQ(s.lastFrom, kNow - kLive);
}

TEST(phase_a_digest_with_spent_budget_reads_nothing) {
    FakeStore s = storeWithTwoPosts();
    const Digest d = digestHaving({"unknownpeerpost0"}, 0);
    const DigestPlan plan = planDigest(d, kNow, kLive, true, /*mayScan=*/false, s.fn());
    CHECK_EQ(s.reads, 0);
    CHECK(!plan.scanned);
    CHECK(plan.missing.empty());
    CHECK(!plan.peerHoldsUnknown);
}

TEST(phase_a_digest_never_offers_posts_outside_live_window) {
    FakeStore s = storeWithTwoPosts();
    // The sender holds nothing, so everything in our window is missing.
    const Digest d = digestHaving({}, 0);
    const DigestPlan plan = planDigest(d, kNow, kLive, false, true, s.fn());
    CHECK_EQ(plan.missing.size(), static_cast<size_t>(1));
    if (!plan.missing.empty())
        CHECK_EQ(plan.missing[0], mkId(1));   // mkId(2) is older than the live window
}

TEST(phase_a_digest_does_not_offer_what_sender_lists) {
    FakeStore s = storeWithTwoPosts();
    const Digest d = digestHaving({mkId(1)}, 0);
    const DigestPlan plan = planDigest(d, kNow, kLive, false, true, s.fn());
    CHECK(plan.missing.empty());
}

// A2: range requests 

TEST(phase_a_invalid_range_does_not_start_the_gap) {
    RangeGate gate;
    Digest bad;
    bad.sinceMs = 5;
    bad.untilMs = 5;   // empty window
    CHECK(!gate.admit(bad, kNow, 3'000, 600'000, 2 * kH).has_value());

    Digest good;
    good.sinceMs = 1;
    good.untilMs = kNow;
    // Same instant: a refused junk request must not have silenced this one.
    CHECK(gate.admit(good, kNow, 3'000, 600'000, 2 * kH).has_value());
}

TEST(phase_a_empty_answer_does_not_start_the_gap) {
    // A valid window with nothing missing is admitted but never answered:
    // the next request must not be silenced by it.
    RangeGate gate;
    Digest good;
    good.sinceMs = 1;
    good.untilMs = kNow;
    CHECK(gate.admit(good, kNow, 3'000, 600'000, 2 * kH).has_value());
    CHECK(gate.admit(good, kNow + 1'000, 3'000, 600'000, 2 * kH).has_value());
}

TEST(phase_a_answered_range_starts_the_gap) {
    RangeGate gate;
    Digest good;
    good.sinceMs = 1;
    good.untilMs = kNow;
    CHECK(gate.admit(good, kNow, 3'000, 600'000, 2 * kH).has_value());
    gate.answered(kNow);   // something was actually sent
    CHECK(!gate.admit(good, kNow + 1'000, 3'000, 600'000, 2 * kH).has_value());
    CHECK(gate.admit(good, kNow + 3'000, 3'000, 600'000, 2 * kH).has_value());
}

TEST(phase_a_range_span_is_trimmed_to_newest) {
    RangeGate gate;
    Digest wide;
    wide.sinceMs = 0;
    wide.untilMs = kNow;
    const auto w = gate.admit(wide, kNow, 3'000, 600'000, 2 * kH);
    CHECK(w.has_value());
    if (w) {
        CHECK_EQ(w->sinceMs, kNow - 2 * kH);
        CHECK_EQ(w->untilMs, kNow);
    }
}

TEST(phase_a_range_future_end_is_clamped_to_skew) {
    RangeGate gate;
    Digest future;
    future.sinceMs = kNow - kH;
    future.untilMs = kNow + 24 * kH;
    const auto w = gate.admit(future, kNow, 3'000, 600'000, 2 * kH);
    CHECK(w.has_value());
    if (w)
        CHECK_EQ(w->untilMs, kNow + 600'000);
}

// A3: image-discovery budget 

TEST(phase_a_mediaask_budget_bounds_distinct_lookups) {
    // Distinct content ids at one instant: only the burst gets through.
    flood::TokenBucket budget(flood::kMediaAskPerMinute, flood::kMediaAskBurst);
    int admitted = 0;
    for (int i = 0; i < 1000; ++i)
        if (budget.take(kNow))
            ++admitted;
    CHECK_EQ(admitted, static_cast<int>(flood::kMediaAskBurst));

    // A minute later, the sustained rate comes back and no more.
    admitted = 0;
    for (int i = 0; i < 1000; ++i)
        if (budget.take(kNow + 60'000))
            ++admitted;
    CHECK_EQ(admitted, static_cast<int>(flood::kMediaAskPerMinute));
}

//  A5: log references 

TEST(phase_a_log_refs_hide_the_tail_of_identity) {
    const std::string account(64, 'f');
    const std::string ref = forumer::shortRef(account);
    CHECK_EQ(ref, std::string(8, 'f') + "...");
    CHECK(ref.size() < account.size());
    CHECK_EQ(forumer::shortRef("short"), std::string("short"));
}

TEST(phase_a_scrub_shortens_every_long_identifier) {
    const std::string cid(59, 'b');   // a content id
    const std::string postId(64, 'a');
    const std::string line = "stored " + postId + " as fr:SAYQ-ZYKX... media " + cid +
                             " at 1744000000000 ms, 3 post(s)";
    const std::string out = forumer::scrubIds(line);
    CHECK(out.find(postId) == std::string::npos);
    CHECK(out.find(cid) == std::string::npos);
    CHECK(out.find(std::string(8, 'a') + "...") != std::string::npos);
    CHECK(out.find("1744000000000") != std::string::npos);   // timestamps stay
    CHECK(out.find("post(s)") != std::string::npos);          // words stay
    CHECK_EQ(forumer::scrubIds("received"), std::string("received"));
}