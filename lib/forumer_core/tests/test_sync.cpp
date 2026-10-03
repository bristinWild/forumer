#include "test.h"

#include <chrono>
#include <cstdio>
#include <string>

#include "forumer_core/bytes.h"
#include "forumer_core/identity.h"
#include "forumer_core/post.h"
#include "forumer_core/sync.h"

using namespace forumer;
using namespace forumer::sync;

namespace {

constexpr int kTestPow = 4;

int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

post::Post makeTopic(const std::string& title = "hello") {
    static auto account = identity::Account::create("A");
    post::Draft d;
    d.kind = post::Kind::Post;
    d.title = title;
    d.body = "body";
    d.domains = {"privacy"};
    d.timestampMs = nowMs();
    return *post::sign(d, account.persona(0), identity::Disclosure::Persona, "", kTestPow);
}


// A 32-hex id whose short form is predictable: "<n as 16 hex>" + 16 zeros.
std::string fakeId(int n) {
    char buf[17];
    std::snprintf(buf, sizeof buf, "%016x", n);
    return std::string(buf) + "0000000000000000";
}

// `count` summaries, newest first, timestamps count..1.
std::vector<PostSummary> held(int count) {
    std::vector<PostSummary> out;
    for (int i = count; i >= 1; --i)
        out.push_back({fakeId(i), i});
    return out;
}

} // namespace

TEST(sync_content_topic) {
    CHECK_EQ(contentTopic("public"), std::string("/forumer/3/public/proto"));
    CHECK_EQ(contentTopic("seminar-2026"), std::string("/forumer/3/seminar-2026/proto"));
    CHECK(contentTopic("").empty());
    CHECK(contentTopic("Public").empty());
    CHECK(contentTopic("a/b").empty());
    CHECK(contentTopic(std::string(65, 'a')).empty());
}

TEST(sync_post_round_trip) {
    const auto p = makeTopic();
    const auto r = decode(encodePost(p));
    CHECK(r.error == DecodeError::None);
    CHECK(r.message.has_value());
    if (r.message && r.message->post) {
        CHECK(r.message->type == MessageType::Post);
        CHECK_EQ(r.message->post->id, p.id);
        CHECK(post::verify(*r.message->post, kTestPow) == post::Error::None);
    }
}

TEST(sync_digest_round_trip) {
    Digest d;
    d.sinceMs = 1234;
    d.have = {shortId(fakeId(1)), shortId(fakeId(2))};
    const auto r = decode(encodeDigest(d));
    CHECK(r.error == DecodeError::None);
    if (r.message) {
        CHECK(r.message->type == MessageType::Digest);
        CHECK_EQ(r.message->digest.sinceMs, int64_t(1234));
        CHECK(r.message->digest.have == d.have);
    }
}

TEST(sync_decode_rejects_junk) {
    CHECK(decode(bytesOf("")).error == DecodeError::Malformed);
    CHECK(decode(bytesOf("hello")).error == DecodeError::Malformed);
    CHECK(decode(bytesOf("[]")).error == DecodeError::Malformed);
    CHECK(decode(bytesOf(R"({"v":1})")).error == DecodeError::Malformed);
    CHECK(decode(bytesOf(R"({"v":1,"t":"poll"})")).error == DecodeError::Malformed);
    CHECK(decode(bytesOf(R"({"v":0,"t":"digest","since":0,"have":[]})")).error == DecodeError::Malformed);
    CHECK(decode(bytesOf(R"({"v":2,"t":"digest","since":0,"have":[]})")).error ==
          DecodeError::UnsupportedVersion);
    CHECK(decode(bytesOf(R"({"v":1,"t":"post","env":"{}"})")).error == DecodeError::Malformed);
    CHECK(decode(bytesOf(R"({"v":1,"t":"post","env":{}})")).error == DecodeError::Malformed);
    // Digest ids must be exactly 16 lowercase hex chars.
    CHECK(decode(bytesOf(R"({"v":1,"t":"digest","since":0,"have":["abc"]})")).error ==
          DecodeError::Malformed);
    CHECK(decode(bytesOf(R"({"v":1,"t":"digest","since":0,"have":["ABCDEF0123456789"]})")).error ==
          DecodeError::Malformed);
    CHECK(decode(bytesOf(R"({"v":1,"t":"digest","since":"0","have":[]})")).error ==
          DecodeError::Malformed);
}

TEST(sync_decode_rejects_oversized) {
    std::vector<uint8_t> big(kMaxPayload + 1, ' ');
    CHECK(decode(big).error == DecodeError::TooLarge);
}

TEST(sync_decode_rejects_overlong_digest) {
    Digest d;
    for (size_t i = 0; i <= kMaxDigestIds; ++i)
        d.have.push_back(shortId(fakeId(static_cast<int>(i))));
    CHECK(decode(encodeDigest(d)).error == DecodeError::Malformed);
}

TEST(sync_full_digest_fits_payload_limit) {
    Digest d;
    d.sinceMs = nowMs();
    for (size_t i = 0; i < kMaxDigestIds; ++i)
        d.have.push_back(shortId(fakeId(static_cast<int>(i))));
    const auto bytes = encodeDigest(d);
    CHECK(bytes.size() <= kMaxPayload);
    CHECK(decode(bytes).error == DecodeError::None);
}

TEST(sync_short_id) {
    CHECK_EQ(shortId(fakeId(255)), std::string("00000000000000ff"));
    CHECK_EQ(shortId("abc"), std::string("abc"));
}

TEST(sync_make_digest_lists_window) {
    const auto h = held(5);  // ts 5..1
    const Digest d = makeDigest(h, 3);
    CHECK_EQ(d.sinceMs, int64_t(3));
    CHECK_EQ(d.have.size(), size_t(3));  // ts 5, 4, 3
    if (d.have.size() == 3)
        CHECK_EQ(d.have[0], shortId(fakeId(5)));
}

TEST(sync_make_digest_truncates_and_moves_since) {
    const auto h = held(10);  // ts 10..1
    const Digest d = makeDigest(h, 0, 4);
    CHECK_EQ(d.have.size(), size_t(4));  // ts 10, 9, 8, 7
    CHECK_EQ(d.sinceMs, int64_t(7));
    // Exactly full but nothing left out: since stays put.
    const Digest exact = makeDigest(held(4), 0, 4);
    CHECK_EQ(exact.sinceMs, int64_t(0));
}

TEST(sync_missing_from_finds_gaps) {
    const auto mine = held(5);  // ts 5..1
    Digest theirs;
    theirs.sinceMs = 2;
    theirs.have = {shortId(fakeId(5)), shortId(fakeId(3))};

    const auto missing = missingFrom(theirs, mine);
    // They lack 4 and 2 (1 is before their window). Newest first.
    CHECK_EQ(missing.size(), size_t(2));
    if (missing.size() == 2) {
        CHECK_EQ(missing[0], fakeId(4));
        CHECK_EQ(missing[1], fakeId(2));
    }
    CHECK_EQ(missingFrom(theirs, mine, 1).size(), size_t(1));
}

TEST(sync_digest_answer_round_trip_converges) {
    // Two peers each missing one of the other's posts end up with the union.
    std::vector<PostSummary> a = {{fakeId(3), 3}, {fakeId(1), 1}};
    std::vector<PostSummary> b = {{fakeId(3), 3}, {fakeId(2), 2}};
    const auto aNeeds = missingFrom(makeDigest(a, 0), b);
    const auto bNeeds = missingFrom(makeDigest(b, 0), a);
    CHECK(aNeeds == std::vector<std::string>{fakeId(2)});
    CHECK(bNeeds == std::vector<std::string>{fakeId(1)});
    // Nothing to answer once both hold everything.
    std::vector<PostSummary> both = {{fakeId(3), 3}, {fakeId(2), 2}, {fakeId(1), 1}};
    CHECK(missingFrom(makeDigest(both, 0), both).empty());
}

TEST(sync_accept_timestamp) {
    const int64_t now = nowMs();
    CHECK(acceptTimestamp(now, now));
    CHECK(acceptTimestamp(now - kWindowMs * 10, now));  // old posts are fine
    CHECK(acceptTimestamp(now + kMaxClockSkewMs, now));
    CHECK(!acceptTimestamp(now + kMaxClockSkewMs + 1, now));
    CHECK(!acceptTimestamp(0, now));
}

TEST(sync_retry_backoff) {
    CHECK_EQ(retryDelayMs(0), int64_t(15'000));
    CHECK_EQ(retryDelayMs(1), int64_t(15'000));
    CHECK_EQ(retryDelayMs(2), int64_t(30'000));
    CHECK_EQ(retryDelayMs(3), int64_t(60'000));
    CHECK_EQ(retryDelayMs(5), int64_t(240'000));
    CHECK_EQ(retryDelayMs(6), int64_t(300'000));
    CHECK_EQ(retryDelayMs(1000), int64_t(300'000));

    OutboxEntry e;
    e.state = SendState::Pending;
    CHECK(dueForRetry(e, 0));  // never attempted
    e.attempts = 1;
    e.lastAttemptMs = 1'000;
    CHECK(!dueForRetry(e, 15'999));
    CHECK(dueForRetry(e, 16'000));
    e.state = SendState::Failed;
    CHECK(dueForRetry(e, 16'000));
    e.state = SendState::Sent;
    CHECK(!dueForRetry(e, 1'000'000'000));
}

TEST(sync_lists_unknown) {
    const auto mine = held(3);  // ts 3..1
    Digest same = makeDigest(mine, 0);
    CHECK(!listsUnknown(same, mine));

    Digest subset;
    subset.have = {shortId(fakeId(2))};
    CHECK(!listsUnknown(subset, mine));  // they hold less: we answer, they don't

    Digest more = same;
    more.have.push_back(shortId(fakeId(9)));
    CHECK(listsUnknown(more, mine));     // they hold a post we lack

    CHECK(!listsUnknown(Digest{}, mine));
    CHECK(listsUnknown(more, {}));
}