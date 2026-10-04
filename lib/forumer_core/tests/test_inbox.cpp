#include "test.h"

#include <filesystem>
#include <string>

#include "forumer_core/crypto.h"
#include "forumer_core/identity.h"
#include "forumer_core/post.h"
#include "forumer_core/post_store.h"

using namespace forumer;
namespace fs = std::filesystem;

namespace {

constexpr int kTestPow = 4;
constexpr int64_t kBase = 1'800'000'000'000;

struct TempDir {
    fs::path path;
    TempDir() {
        path = fs::temp_directory_path() / ("forumer-test-" + toHex(crypto::randomBytes(6)));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

post::Post topic(const identity::Persona& by, const std::string& title, int64_t ts) {
    post::Draft d;
    d.kind = post::Kind::Post;
    d.title = title;
    d.domains = {"privacy"};
    d.timestampMs = ts;
    return *post::sign(d, by, identity::Disclosure::Persona, "", kTestPow);
}

post::Post reply(const identity::Persona& by, const std::string& root, const std::string& parent,
                 int64_t ts) {
    post::Draft d;
    d.kind = post::Kind::Reply;
    d.root = root;
    d.parent = parent;
    d.body = "reply at " + std::to_string(ts);
    d.timestampMs = ts;
    return *post::sign(d, by, identity::Disclosure::Persona, "", kTestPow);
}

// Store a post as written on this device by `account` (in the outbox).
void mine(PostStore& store, const post::Post& p, const std::string& account) {
    store.insert(p, kBase);
    store.enqueue(p.id, account, kBase);
}

} // namespace

TEST(inbox_collects_replies_to_me_and_in_threads_i_joined) {
    TempDir dir;
    auto store = PostStore::open(dir.path / "posts.sqlite3");
    auto me = identity::Account::create("me");
    auto them = identity::Account::create("them");

    const auto myTopic = topic(me.persona(0), "mine", kBase);
    const auto theirTopic = topic(them.persona(0), "theirs", kBase);
    mine(*store, myTopic, "me");
    store->insert(theirTopic, kBase);

    // 1: answers my topic (direct)
    const auto r1 = reply(them.persona(0), myTopic.id, myTopic.id, kBase + 1);
    // 2: my own reply in their topic, then 3: an answer to it (direct)
    const auto r2 = reply(me.persona(1), theirTopic.id, theirTopic.id, kBase + 2);
    const auto r3 = reply(them.persona(0), theirTopic.id, r2.id, kBase + 3);
    // 4: someone answering r1 inside my topic (not direct: in my topic)
    const auto r4 = reply(them.persona(1), myTopic.id, r1.id, kBase + 4);
    // 5: a later reply in their topic, a thread I joined with r2 (not direct)
    const auto r5 = reply(them.persona(1), theirTopic.id, theirTopic.id, kBase + 5);
    // 0: a reply in their topic from before I joined - not news to me
    const auto r0 = reply(them.persona(2), theirTopic.id, theirTopic.id, kBase + 1);
    // 6: my own reply in my topic - never in my inbox
    const auto r6 = reply(me.persona(0), myTopic.id, r1.id, kBase + 6);

    store->insert(r1, kBase);
    mine(*store, r2, "me");
    store->insert(r3, kBase);
    store->insert(r4, kBase);
    store->insert(r5, kBase);
    store->insert(r0, kBase);
    mine(*store, r6, "me");

    const auto items = store->inbox("me");
    CHECK_EQ(items.size(), size_t(4));
    if (items.size() == 4) {
        CHECK_EQ(items[0].postId, r5.id);   // newest first
        CHECK(!items[0].direct);
        CHECK(!items[0].inMyTopic);         // a thread I joined
        CHECK_EQ(items[1].postId, r4.id);
        CHECK(!items[1].direct);
        CHECK(items[1].inMyTopic);
        CHECK_EQ(items[2].postId, r3.id);
        CHECK(items[2].direct);
        CHECK_EQ(items[2].rootId, theirTopic.id);
        CHECK_EQ(items[3].postId, r1.id);
        CHECK(items[3].direct);
        CHECK(items[3].inMyTopic);
        CHECK(!items[3].read);
    }
    CHECK_EQ(store->unreadCount("me"), size_t(4));

    // Another account on the same device has its own (empty) inbox.
    CHECK_EQ(store->inbox("other").size(), size_t(0));
    CHECK_EQ(store->unreadCount("other"), size_t(0));
}

TEST(inbox_reply_to_a_second_level_reply_reaches_its_author) {
    // Threads are two levels deep: answering a level-2 reply attaches the
    // answer to the level-1 reply above it. The level-2 author still hears.
    TempDir dir;
    auto store = PostStore::open(dir.path / "posts.sqlite3");
    auto a = identity::Account::create("a");
    auto b = identity::Account::create("b");

    const auto t = topic(a.persona(0), "a's topic", kBase);
    const auto l1 = reply(a.persona(0), t.id, t.id, kBase + 1);      // a, level 1
    const auto l2 = reply(b.persona(0), t.id, l1.id, kBase + 2);     // b answers it
    const auto back = reply(a.persona(0), t.id, l1.id, kBase + 3);   // a "replies to b"
    store->insert(t, kBase);
    store->insert(l1, kBase);
    mine(*store, l2, "b");
    store->insert(back, kBase);

    const auto items = store->inbox("b");
    CHECK_EQ(items.size(), size_t(1));
    if (!items.empty()) {
        CHECK_EQ(items[0].postId, back.id);
        CHECK(!items[0].direct);
        CHECK(!items[0].inMyTopic);
    }
}

TEST(inbox_read_state_is_per_account_and_ignores_strangers) {
    TempDir dir;
    auto store = PostStore::open(dir.path / "posts.sqlite3");
    auto me = identity::Account::create("me");
    auto them = identity::Account::create("them");

    const auto myTopic = topic(me.persona(0), "mine", kBase);
    mine(*store, myTopic, "me");
    const auto a = reply(them.persona(0), myTopic.id, myTopic.id, kBase + 1);
    const auto b = reply(them.persona(1), myTopic.id, myTopic.id, kBase + 2);
    const auto unrelated = topic(them.persona(0), "unrelated", kBase + 3);
    store->insert(a, kBase);
    store->insert(b, kBase);
    store->insert(unrelated, kBase);

    CHECK(store->markRead("me", {a.id, unrelated.id, "nope"}, kBase + 10));
    CHECK_EQ(store->unreadCount("me"), size_t(1));
    const auto items = store->inbox("me");
    CHECK_EQ(items.size(), size_t(2));
    if (items.size() == 2) {
        CHECK(!items[0].read);  // b
        CHECK(items[1].read);   // a
    }
    // Marking twice is harmless.
    CHECK(store->markRead("me", {a.id}, kBase + 11));
    CHECK_EQ(store->unreadCount("me"), size_t(1));

    CHECK(store->markAllRead("me", kBase + 12));
    CHECK_EQ(store->unreadCount("me"), size_t(0));

    // A reply arriving later is unread again.
    const auto c = reply(them.persona(2), myTopic.id, b.id, kBase + 20);
    store->insert(c, kBase + 20);
    CHECK_EQ(store->unreadCount("me"), size_t(1));
}

TEST(inbox_limit_keeps_the_newest) {
    TempDir dir;
    auto store = PostStore::open(dir.path / "posts.sqlite3");
    auto me = identity::Account::create("me");
    auto them = identity::Account::create("them");
    const auto myTopic = topic(me.persona(0), "mine", kBase);
    mine(*store, myTopic, "me");
    std::string newest;
    for (int i = 0; i < 5; ++i) {
        const auto r = reply(them.persona(i), myTopic.id, myTopic.id, kBase + 100 + i);
        store->insert(r, kBase);
        newest = r.id;
    }
    const auto items = store->inbox("me", 2);
    CHECK_EQ(items.size(), size_t(2));
    if (!items.empty())
        CHECK_EQ(items[0].postId, newest);
    CHECK_EQ(store->unreadCount("me"), size_t(5));
}