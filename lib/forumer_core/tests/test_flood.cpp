#include "test.h"

#include <chrono>
#include <filesystem>
#include <string>

#include "forumer_core/crypto.h"
#include "forumer_core/flood.h"
#include "forumer_core/identity.h"
#include "forumer_core/post.h"
#include "forumer_core/post_store.h"

using namespace forumer;
using namespace forumer::flood;
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

post::Post topicAt(const identity::Persona& persona, int64_t ts, const std::string& title) {
    post::Draft d;
    d.kind = post::Kind::Post;
    d.title = title;
    d.domains = {"privacy"};
    d.timestampMs = ts;
    return *post::sign(d, persona, identity::Disclosure::Persona, "", kTestPow);
}

} // namespace

TEST(flood_limits_per_kind) {
    CHECK_EQ(maxPerWindow(post::Kind::Post), kMaxTopics);
    CHECK_EQ(maxPerWindow(post::Kind::Reply), kMaxReplies);
    CHECK(withinLimit(post::Kind::Post, kMaxTopics - 1));
    CHECK(!withinLimit(post::Kind::Post, kMaxTopics));
    CHECK(withinLimit(post::Kind::Reply, kMaxTopics));
    CHECK(!withinLimit(post::Kind::Reply, kMaxReplies));
}

TEST(flood_wait_until_the_window_has_room) {
    const int64_t now = 10 * kWindowMs;
    std::vector<int64_t> ts;
    for (int i = 0; i < kMaxTopics - 1; ++i) ts.push_back(now - 1000);
    CHECK_EQ(waitMs(post::Kind::Post, ts, now), int64_t(0));   // one slot left

    ts.push_back(now - 30 * 60 * 1000);                        // window now full; oldest 30 min ago
    const int64_t wait = waitMs(post::Kind::Post, ts, now);
    CHECK(wait > 29 * 60 * 1000);
    CHECK(wait <= 30 * 60 * 1000 + 1);
}

TEST(flood_token_bucket) {
    TokenBucket bucket(60, 3);   // one a second, three saved
    int64_t t = 1000;
    CHECK(bucket.take(t));
    CHECK(bucket.take(t));
    CHECK(bucket.take(t));
    CHECK(!bucket.take(t));          // empty
    CHECK(!bucket.take(t + 500));    // half a token
    CHECK(bucket.take(t + 1000));    // refilled one
    CHECK(!bucket.take(t + 1000));
    // Long idle: refills only up to the burst.
    CHECK(bucket.available(t + 10 * 60 * 1000) <= 3.0 + 1e-9);
    CHECK(bucket.take(t + 600000));
    CHECK(bucket.take(t + 600000));
    CHECK(bucket.take(t + 600000));
    CHECK(!bucket.take(t + 600000));
}

TEST(flood_token_bucket_ignores_clock_going_back) {
    TokenBucket bucket(60, 1);
    CHECK(bucket.take(5000));
    CHECK(!bucket.take(1000));       // earlier time: no free refill
    CHECK(bucket.take(6000));
}

TEST(flood_store_counts_by_author_and_window) {
    TempDir dir;
    auto store = PostStore::open(dir.path / "posts.sqlite3");
    auto account = identity::Account::create("A");
    const auto me = account.persona(0);
    const auto other = account.persona(1);
    const int64_t base = 100 * kWindowMs;

    for (int i = 0; i < 4; ++i)
        store->insert(topicAt(me, base + i * 1000, "t" + std::to_string(i)), base);
    store->insert(topicAt(me, base - 2 * kWindowMs, "old"), base);
    store->insert(topicAt(other, base, "someone else"), base);

    CHECK_EQ(store->countByAuthor(me.publicKey(), post::Kind::Post, base - kWindowMs, base + kWindowMs), size_t(4));
    CHECK_EQ(store->countByAuthor(me.publicKey(), post::Kind::Reply, base - kWindowMs, base + kWindowMs), size_t(0));
    CHECK_EQ(store->countByAuthor(other.publicKey(), post::Kind::Post, base, base), size_t(1));
    CHECK(store->hasAuthor(me.publicKey()));
    CHECK(!store->hasAuthor(account.persona(9).publicKey()));
}

TEST(flood_store_counts_own_posts_across_personas) {
    TempDir dir;
    auto store = PostStore::open(dir.path / "posts.sqlite3");
    auto account = identity::Account::create("A");
    const int64_t base = 100 * kWindowMs;

    // Three topics from three different personas, all by this account.
    for (int i = 0; i < 3; ++i) {
        const auto p = topicAt(account.persona(i), base + i, "mine " + std::to_string(i));
        store->insert(p, base);
        store->enqueue(p.id, "acct", base);
    }
    // One from another account on the same device, one received.
    const auto theirs = topicAt(account.persona(5), base, "other account");
    store->insert(theirs, base);
    store->enqueue(theirs.id, "other", base);
    store->insert(topicAt(account.persona(6), base, "received"), base);

    CHECK_EQ(store->countOwn("acct", post::Kind::Post, base - kWindowMs), size_t(3));
    CHECK_EQ(store->countOwn("acct", post::Kind::Post, base + 10), size_t(0));
    CHECK_EQ(store->countOwn("other", post::Kind::Post, 0), size_t(1));

    const auto ts = store->ownTimestamps("acct", post::Kind::Post, base - kWindowMs);
    CHECK_EQ(ts.size(), size_t(3));
    if (ts.size() == 3) {
        CHECK_EQ(ts[0], base);       // oldest first
        CHECK_EQ(ts[2], base + 2);
    }
}