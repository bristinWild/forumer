#include "test.h"

#include <filesystem>
#include <string>

#include "forumer_core/crypto.h"
#include "forumer_core/identity.h"
#include "forumer_core/post.h"
#include "forumer_core/post_store.h"
#include "forumer_core/sync.h"

using namespace forumer;
using namespace forumer::sync;
namespace fs = std::filesystem;

namespace {

constexpr int kTestPow = 4;
constexpr int64_t kDay = 24LL * 60 * 60 * 1000;
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

post::Post topicAt(const identity::Persona& by, int64_t ts, const std::string& title) {
    post::Draft d;
    d.kind = post::Kind::Post;
    d.title = title;
    d.domains = {"privacy"};
    d.timestampMs = ts;
    return *post::sign(d, by, identity::Disclosure::Persona, "", kTestPow);
}

std::string hexId(int i) {
    char buf[33];
    std::snprintf(buf, sizeof buf, "%032x", i);
    return buf;
}

} // namespace

TEST(range_request_round_trips) {
    Digest r;
    r.sinceMs = kBase - 7 * kDay;
    r.untilMs = kBase;
    r.have = {shortId(hexId(1)), shortId(hexId(2))};
    const auto decoded = decode(encodeRange(r));
    CHECK(decoded.message.has_value());
    if (decoded.message) {
        CHECK(decoded.message->type == MessageType::Range);
        CHECK_EQ(decoded.message->digest.sinceMs, r.sinceMs);
        CHECK_EQ(decoded.message->digest.untilMs, r.untilMs);
        CHECK_EQ(decoded.message->digest.have.size(), size_t(2));
    }
}

TEST(range_request_needs_a_valid_window) {
    const std::string noUntil = R"({"v":1,"t":"range","since":5,"have":[]})";
    const std::string backwards = R"({"v":1,"t":"range","since":5,"until":5,"have":[]})";
    CHECK(decode({noUntil.begin(), noUntil.end()}).error == DecodeError::Malformed);
    CHECK(decode({backwards.begin(), backwards.end()}).error == DecodeError::Malformed);
}

TEST(digest_carries_oldest_and_stays_compatible) {
    Digest d;
    d.sinceMs = kBase;
    d.oldestMs = kBase - 30 * kDay;
    const auto decoded = decode(encodeDigest(d));
    CHECK(decoded.message.has_value());
    if (decoded.message) {
        CHECK(decoded.message->type == MessageType::Digest);
        CHECK_EQ(decoded.message->digest.oldestMs, d.oldestMs);
        CHECK_EQ(decoded.message->digest.untilMs, int64_t(0));
    }
    // A digest from an older build has no "oldest".
    const std::string old = R"({"v":1,"t":"digest","since":5,"have":[]})";
    const auto plain = decode({old.begin(), old.end()});
    CHECK(plain.message.has_value());
    if (plain.message)
        CHECK_EQ(plain.message->digest.oldestMs, int64_t(0));
}

TEST(range_digest_and_answers_stay_inside_the_window) {
    // held: newest first, one post a day for 10 days
    std::vector<PostSummary> held;
    for (int i = 0; i < 10; ++i)
        held.push_back({hexId(100 + i), kBase - i * kDay});
    const int64_t from = kBase - 6 * kDay, to = kBase - 2 * kDay;   // days 3..6 (until is exclusive)

    const Digest mine = makeDigest(held, from, kMaxDigestIds, to);
    CHECK_EQ(mine.have.size(), size_t(4));
    CHECK_EQ(mine.untilMs, to);

    // A requester holding nothing in the window gets exactly those four.
    Digest empty;
    empty.sinceMs = from;
    empty.untilMs = to;
    const auto missing = missingFrom(empty, held);
    CHECK_EQ(missing.size(), size_t(4));
    if (missing.size() == 4) {
        CHECK_EQ(missing.front(), hexId(103));   // newest first
        CHECK_EQ(missing.back(), hexId(106));
    }
    // Holding them all: nothing to send.
    CHECK_EQ(missingFrom(mine, held).size(), size_t(0));
}

TEST(history_window_shrinks_when_we_already_hold_a_lot) {
    const int64_t floor = kBase;
    // Sparse: a full week, but never below the target.
    CHECK_EQ(historyWindowStart(floor, 0, [](int64_t, int64_t) { return size_t(10); }),
             floor - kHistorySpanMs);
    CHECK_EQ(historyWindowStart(floor, floor - 2 * kDay, [](int64_t, int64_t) { return size_t(10); }),
             floor - 2 * kDay);
    // Dense: 100 posts an hour -> halve until the window lists <= kHistoryMaxHeld.
    auto dense = [](int64_t from, int64_t to) { return size_t((to - from) / (60 * 60 * 1000) * 100); };
    const int64_t start = historyWindowStart(floor, 0, dense);
    CHECK(dense(start, floor) <= kHistoryMaxHeld);
    CHECK(floor - start >= kHistoryMinSpanMs);
}

TEST(post_store_range_oldest_and_meta) {
    TempDir dir;
    auto store = PostStore::open(dir.path / "posts.sqlite3");
    auto account = identity::Account::create("A");
    CHECK(!store->oldestTimestamp().has_value());
    for (int i = 0; i < 5; ++i)
        store->insert(topicAt(account.persona(i), kBase - i * kDay, "t" + std::to_string(i)), kBase);

    CHECK_EQ(store->oldestTimestamp().value_or(0), kBase - 4 * kDay);
    const auto mid = store->range(kBase - 3 * kDay, kBase - kDay);   // days 3 and 2
    CHECK_EQ(mid.size(), size_t(2));
    if (mid.size() == 2)
        CHECK_EQ(mid[0].timestampMs, kBase - 2 * kDay);
    CHECK_EQ(store->countRange(kBase - 10 * kDay, kBase + 1), size_t(5));

    CHECK(!store->meta("history_floor").has_value());
    CHECK(store->setMeta("history_floor", "123"));
    CHECK(store->setMeta("history_floor", "456"));
    CHECK_EQ(store->meta("history_floor").value_or(""), std::string("456"));
}