#include "test.h"

#include <chrono>
#include <string>

#include "forumer_core/identity.h"
#include "forumer_core/post.h"
#include "forumer_core/thread.h"

using namespace forumer;
using namespace forumer::thread;

namespace {

constexpr int kTestPow = 4;

int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

identity::Account& account() {
    static auto a = identity::Account::create("A");
    return a;
}

post::Post topic(const std::string& title = "a topic", const std::string& forum = "public") {
    post::Draft d;
    d.kind = post::Kind::Post;
    d.forum = forum;
    d.title = title;
    d.domains = {"privacy"};
    d.timestampMs = nowMs();
    return *post::sign(d, account().persona(0), identity::Disclosure::Persona, "", kTestPow);
}

post::Post replyAt(const Target& t, const std::string& body = "a reply") {
    post::Draft d;
    d.kind = post::Kind::Reply;
    d.root = t.root;
    d.parent = t.parent;
    d.body = body;
    d.timestampMs = nowMs();
    return *post::sign(d, account().persona(1), identity::Disclosure::Persona, "", kTestPow);
}

} // namespace

TEST(thread_reply_targets_stay_within_two_levels) {
    const auto t = topic();

    const Target l1 = replyTarget(t);
    CHECK_EQ(l1.root, t.id);
    CHECK_EQ(l1.parent, t.id);
    const auto r1 = replyAt(l1);
    CHECK_EQ(depthOf(r1), 1);

    const Target l2 = replyTarget(r1);
    CHECK_EQ(l2.root, t.id);
    CHECK_EQ(l2.parent, r1.id);
    const auto r2 = replyAt(l2);
    CHECK_EQ(depthOf(r2), 2);

    // Answering a level-2 reply joins the same sub-thread.
    const Target again = replyTarget(r2);
    CHECK_EQ(again.root, t.id);
    CHECK_EQ(again.parent, r1.id);
    CHECK_EQ(depthOf(replyAt(again)), 2);

    CHECK_EQ(depthOf(t), 0);
}

TEST(thread_placement_accepts_valid_replies) {
    const auto t = topic();
    const auto r1 = replyAt(replyTarget(t));
    const auto r2 = replyAt(replyTarget(r1));
    CHECK(checkPlacement(t, std::nullopt) == Placement::Ok);
    CHECK(checkPlacement(r1, t) == Placement::Ok);
    CHECK(checkPlacement(r2, r1) == Placement::Ok);
    // Parent not here yet: accepted for now.
    CHECK(checkPlacement(r2, std::nullopt) == Placement::Ok);
}

TEST(thread_placement_rejects_a_third_level) {
    const auto t = topic();
    const auto r1 = replyAt(replyTarget(t));
    const auto r2 = replyAt(replyTarget(r1));
    // A forged reply whose parent is the level-2 reply.
    const auto r3 = replyAt({t.id, r2.id});
    CHECK(checkPlacement(r3, r2) == Placement::TooDeep);
}

TEST(thread_placement_rejects_cross_thread_parents) {
    const auto t1 = topic("one");
    const auto t2 = topic("two");
    const auto r1 = replyAt(replyTarget(t1));
    // Claims to be in t2 but answers a reply that lives in t1.
    const auto forged = replyAt({t2.id, r1.id});
    CHECK(checkPlacement(forged, r1) == Placement::WrongThread);
    // Claims the topic is its parent, but the "topic" is a reply.
    const auto forged2 = replyAt({r1.id, r1.id});
    CHECK(checkPlacement(forged2, r1) == Placement::WrongThread);
    // Parent from another forum (replyAt always signs for "public").
    const auto other = topic("elsewhere", "seminar");
    const auto cross = replyAt(replyTarget(other));
    CHECK(checkPlacement(cross, other) == Placement::WrongThread);
}

TEST(thread_placement_names) {
    CHECK(std::string(describe(Placement::TooDeep)).find("two levels") != std::string::npos);
}