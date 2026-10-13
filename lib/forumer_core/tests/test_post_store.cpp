#include "test.h"

#include <chrono>
#include <filesystem>

#include "forumer_core/crypto.h"
#include "forumer_core/identity.h"
#include "forumer_core/post.h"
#include "forumer_core/post_store.h"

using namespace forumer;
namespace fs = std::filesystem;

namespace {

constexpr int kTestPow = 4;

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

int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

post::Post makeTopic(const identity::Persona& persona, const std::string& title, int64_t ts) {
    post::Draft d;
    d.kind = post::Kind::Post;
    d.title = title;
    d.body = "body of " + title;
    d.domains = {"privacy"};
    d.timestampMs = ts;
    return *post::sign(d, persona, identity::Disclosure::Persona, "", kTestPow);
}

post::Post makeReply(const identity::Persona& persona, const post::Post& topic, int64_t ts) {
    post::Draft d;
    d.kind = post::Kind::Reply;
    d.root = topic.id;
    d.parent = topic.id;
    d.body = "a reply";
    d.timestampMs = ts;
    return *post::sign(d, persona, identity::Disclosure::Anonymous, "", kTestPow);
}

} // namespace

TEST(post_store_opens_and_creates_parent_dirs) {
    TempDir dir;
    std::string err;
    auto store = PostStore::open(dir.path / "nested" / "posts.sqlite3", &err);
    CHECK(store != nullptr);
    CHECK(err.empty());
    if (store)
        CHECK_EQ(store->count(), size_t(0));
}

TEST(post_store_insert_is_first_copy_wins) {
    TempDir dir;
    auto store = PostStore::open(dir.path / "posts.sqlite3");
    auto account = identity::Account::create("A");
    const auto t = makeTopic(account.persona(0), "hello", nowMs());

    CHECK(store->insert(t, nowMs()) == PostStore::Insert::Added);
    CHECK(store->insert(t, nowMs()) == PostStore::Insert::Duplicate);
    CHECK_EQ(store->count(), size_t(1));
    CHECK(store->contains(t.id));
    CHECK(!store->contains("00000000000000000000000000000000"));

    auto back = store->get(t.id);
    CHECK(back.has_value());
    if (back) {
        CHECK_EQ(back->id, t.id);
        CHECK_EQ(back->content.title, std::string("hello"));
        CHECK(post::verify(*back, kTestPow) == post::Error::None);  // bytes survive storage
    }
}

TEST(post_store_all_orders_topics_then_replies_oldest_first) {
    TempDir dir;
    auto store = PostStore::open(dir.path / "posts.sqlite3");
    auto account = identity::Account::create("A");
    const int64_t base = nowMs() - 10'000;

    const auto newer = makeTopic(account.persona(0), "newer", base + 2000);
    const auto older = makeTopic(account.persona(0), "older", base + 1000);
    const auto reply = makeReply(account.persona(1), older, base + 500);  // reply dated earliest

    store->insert(reply, base);
    store->insert(newer, base);
    store->insert(older, base);

    const auto all = store->all();
    CHECK_EQ(all.size(), size_t(3));
    if (all.size() == 3) {
        CHECK_EQ(all[0].id, older.id);
        CHECK_EQ(all[1].id, newer.id);
        CHECK_EQ(all[2].id, reply.id);
    }
}

TEST(post_store_recent_is_newest_first_from_since) {
    TempDir dir;
    auto store = PostStore::open(dir.path / "posts.sqlite3");
    auto account = identity::Account::create("A");
    const int64_t base = nowMs() - 10'000;

    const auto a = makeTopic(account.persona(0), "a", base + 1);
    const auto b = makeTopic(account.persona(0), "b", base + 2);
    const auto c = makeTopic(account.persona(0), "c", base + 3);
    for (const auto* p : {&a, &b, &c})
        store->insert(*p, base);

    const auto r = store->recent(base + 2);
    CHECK_EQ(r.size(), size_t(2));
    if (r.size() == 2) {
        CHECK_EQ(r[0].id, c.id);
        CHECK_EQ(r[1].id, b.id);
        CHECK_EQ(r[0].timestampMs, base + 3);
    }
}

TEST(post_store_authors_lists_each_signer_once) {
    TempDir dir;
    auto store = PostStore::open(dir.path / "posts.sqlite3");
    auto account = identity::Account::create("A");
    const auto t1 = makeTopic(account.persona(0), "one", nowMs());
    const auto t2 = makeTopic(account.persona(0), "two", nowMs());
    const auto r = makeReply(account.persona(3), t1, nowMs());
    store->insert(t1, 0);
    store->insert(t2, 0);
    store->insert(r, 0);

    const auto authors = store->authors();
    CHECK_EQ(authors.size(), size_t(2));
    CHECK(authors.count(account.persona(0).publicKey()) == 1);
    CHECK(authors.count(account.persona(3).publicKey()) == 1);
}

TEST(post_store_survives_reopen) {
    TempDir dir;
    auto account = identity::Account::create("A");
    const auto t = makeTopic(account.persona(0), "durable", nowMs());
    OwnData saved;
    {
        auto store = PostStore::open(dir.path / "posts.sqlite3");
        store->insert(t, 1);
        store->enqueue(t.id, "acct", 1);
        saved = store->exportOwn("acct");
    }
    auto store = PostStore::open(dir.path / "posts.sqlite3");
    CHECK(store->contains(t.id));
    // F3: the outbox is not in the log file; it comes back from the
    // account's own (encrypted) data.
    CHECK(!store->outboxEntry(t.id).has_value());
    CHECK(store->importOwn(saved));
    CHECK(store->outboxEntry(t.id).has_value());
}

TEST(outbox_lifecycle) {
    TempDir dir;
    auto store = PostStore::open(dir.path / "posts.sqlite3");
    auto account = identity::Account::create("A");
    const auto t = makeTopic(account.persona(0), "mine", nowMs());

    CHECK(!store->enqueue(t.id, "acct", 100));  // the post must be stored first
    store->insert(t, 100);
    CHECK(store->enqueue(t.id, "acct", 100));
    CHECK(store->enqueue(t.id, "acct", 999));   // idempotent

    auto e = store->outboxEntry(t.id);
    CHECK(e.has_value());
    if (e) {
        CHECK(e->state == SendState::Pending);
        CHECK_EQ(e->attempts, 0);
        CHECK_EQ(e->createdMs, int64_t(100));
        CHECK_EQ(e->lastAttemptMs, int64_t(0));
        CHECK_EQ(e->accountId, std::string("acct"));
    }

    CHECK(store->markAttempt(t.id, 200));
    CHECK(store->markState(t.id, SendState::Failed, "no peers"));
    e = store->outboxEntry(t.id);
    if (e) {
        CHECK(e->state == SendState::Failed);
        CHECK_EQ(e->attempts, 1);
        CHECK_EQ(e->lastAttemptMs, int64_t(200));
        CHECK_EQ(e->lastError, std::string("no peers"));
    }
    CHECK_EQ(store->unsent().size(), size_t(1));

    CHECK(store->markAttempt(t.id, 300));
    e = store->outboxEntry(t.id);
    if (e) {
        CHECK(e->state == SendState::Pending);  // a new attempt is in flight
        CHECK_EQ(e->attempts, 2);
    }

    CHECK(store->markState(t.id, SendState::Sent, "ignored"));
    e = store->outboxEntry(t.id);
    if (e) {
        CHECK(e->state == SendState::Sent);
        CHECK(e->lastError.empty());
    }
    CHECK(store->unsent().empty());
    CHECK_EQ(store->outbox().size(), size_t(1));

    CHECK(!store->markState("nope", SendState::Sent));
    CHECK(!store->markAttempt("nope", 1));
}

TEST(outbox_lists_are_ordered) {
    TempDir dir;
    auto store = PostStore::open(dir.path / "posts.sqlite3");
    auto account = identity::Account::create("A");
    const auto first = makeTopic(account.persona(0), "first", nowMs());
    const auto second = makeTopic(account.persona(0), "second", nowMs());
    store->insert(first, 0);
    store->insert(second, 0);
    store->enqueue(first.id, "acct", 10);
    store->enqueue(second.id, "acct", 20);

    const auto all = store->outbox();  // newest first
    CHECK_EQ(all.size(), size_t(2));
    if (all.size() == 2)
        CHECK_EQ(all[0].postId, second.id);

    const auto unsent = store->unsent();  // oldest first
    CHECK_EQ(unsent.size(), size_t(2));
    if (unsent.size() == 2)
        CHECK_EQ(unsent[0].postId, first.id);
}

TEST(send_state_names_round_trip) {
    for (auto s : {SendState::Pending, SendState::Sent, SendState::Failed})
        CHECK(sendStateFrom(toString(s)) == s);
    CHECK(!sendStateFrom("delivered").has_value());
}