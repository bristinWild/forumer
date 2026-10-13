#include "test.h"

#include <string>

#include "forumer_core/crypto.h"
#include "forumer_core/forums.h"
#include "forumer_core/identity.h"
#include "forumer_core/media.h"
#include "forumer_core/post.h"

using namespace forumer;

namespace {

// Enough of each format for sniff(): the real decoding happens in the app.
Bytes fakePng(size_t n) {
    Bytes b = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    while (b.size() < n) b.push_back(static_cast<uint8_t>(b.size() * 31));
    return b;
}
Bytes fakeJpeg(size_t n) {
    Bytes b = {0xff, 0xd8, 0xff, 0xe0};
    while (b.size() < n) b.push_back(static_cast<uint8_t>(b.size() * 7));
    return b;
}

post::MediaRef refFor(const media::Sealed& s, const std::string& mime) {
    return {"zDvZRwzm7J5w8iY3s2Kq4cW8vHTPrnBDqrsxyUe8D4ycSv2E", mime, s.size, s.sha256};
}

} // namespace

TEST(media_seal_and_open_round_trip) {
    const Bytes img = fakePng(5000);
    auto s = media::seal(img, "image/png");
    CHECK(s.has_value());
    if (!s) return;
    CHECK_EQ(s->size, uint64_t(5000));
    CHECK_EQ(s->blob.size(), img.size() + media::kBlobOverhead);
    CHECK_EQ(s->sha256, media::sha256Hex(img.data(), img.size()));
    // Nothing of the image is readable in the blob.
    CHECK(std::string(s->blob.begin(), s->blob.end()).find("PNG") == std::string::npos);

    auto back = media::open(s->blob, refFor(*s, "image/png"));
    CHECK(back.has_value() && *back == img);

    // Convergent: the same image seals to the same blob.
    CHECK(media::seal(img, "image/png")->blob == s->blob);
}

TEST(media_refuses_what_the_author_did_not_sign) {
    const Bytes img = fakeJpeg(3000);
    auto s = *media::seal(img, "image/jpeg");
    auto ref = refFor(s, "image/jpeg");

    auto tampered = s.blob;
    tampered[tampered.size() / 2] ^= 1;
    CHECK(!media::open(tampered, ref).has_value());

    // Another image's blob under this reference.
    auto other = *media::seal(fakeJpeg(3000 + 1), "image/jpeg");
    CHECK(!media::open(other.blob, ref).has_value());

    auto wrongMime = ref;
    wrongMime.mime = "image/png";
    CHECK(!media::open(s.blob, wrongMime).has_value());

    auto wrongSize = ref;
    wrongSize.size += 1;
    CHECK(!media::open(s.blob, wrongSize).has_value());

    auto wrongHash = ref;
    wrongHash.sha256[0] = wrongHash.sha256[0] == 'a' ? 'b' : 'a';
    CHECK(!media::open(s.blob, wrongHash).has_value());

    CHECK(!media::open(Bytes(), ref).has_value());
}

TEST(media_private_forum_images_need_the_forum_key) {
    auto f = *forums::create("Club");
    auto g = *forums::create("Other");
    const Bytes img = fakePng(4000);
    auto inForum = *media::seal(img, "image/png", &f.key);
    auto pub = *media::seal(img, "image/png");
    CHECK(inForum.blob != pub.blob);                  // outsiders can't match it to a known image
    CHECK(inForum.sha256 == pub.sha256);
    auto ref = refFor(inForum, "image/png");
    CHECK(media::open(inForum.blob, ref, &f.key).has_value());
    CHECK(!media::open(inForum.blob, ref).has_value());
    CHECK(!media::open(inForum.blob, ref, &g.key).has_value());
}

TEST(media_only_allowed_images_within_the_limit) {
    CHECK(!media::seal(fakePng(100), "image/gif").has_value());
    CHECK(!media::seal(fakePng(100), "image/jpeg").has_value());       // bytes say PNG
    CHECK(!media::seal(Bytes(100, 'x'), "image/png").has_value());     // not an image at all
    CHECK(!media::seal(Bytes(), "image/png").has_value());
    CHECK(!media::seal(fakePng(media::kMaxImageBytes + 1), "image/png").has_value());
    CHECK(media::seal(fakePng(media::kMaxImageBytes), "image/png").has_value());

    CHECK_EQ(media::sniff(fakeJpeg(10)), std::string("image/jpeg"));
    CHECK_EQ(media::sniff(Bytes{'G', 'I', 'F', '8', '9', 'a'}), std::string());
}

TEST(media_references_are_checked_before_fetching) {
    auto s = *media::seal(fakePng(100), "image/png");
    auto ref = refFor(s, "image/png");
    CHECK(media::usable(ref));
    auto r = ref; r.cid = "short";                         CHECK(!media::usable(r));
    r = ref; r.cid = "../../etc/passwd/aaaaaaaaaaaaaaaaa"; CHECK(!media::usable(r));
    r = ref; r.mime = "image/svg+xml";                     CHECK(!media::usable(r));
    r = ref; r.size = media::kMaxImageBytes + 1;           CHECK(!media::usable(r));
    r = ref; r.size = 0;                                   CHECK(!media::usable(r));
    r = ref; r.sha256 = std::string(64, 'G');              CHECK(!media::usable(r));
}

TEST(media_references_survive_signing) {
    // MediaRef has been part of the signed post since 0.2.2: a post with
    // images signs, verifies and round-trips unchanged.
    identity::Account a = identity::Account::create("x");
    auto s = *media::seal(fakeJpeg(2000), "image/jpeg");
    post::Draft d;
    d.kind = post::Kind::Post;
    d.title = "with a picture";
    d.body = "see below";
    d.domains = {"general"};
    d.timestampMs = 1'800'000'000'000;
    d.media = {refFor(s, "image/jpeg")};
    auto p = post::sign(d, a.currentPersona(), identity::Disclosure::Persona, "", 4);
    CHECK(p.has_value());
    if (!p) return;
    CHECK(post::verify(*p, 4) == post::Error::None);
    auto back = post::Post::fromJson(p->toJson());
    CHECK(back && back->content.media.size() == 1 && back->content.media[0].sha256 == s.sha256);
    auto tampered = *back;
    tampered.content.media[0].cid[5] = tampered.content.media[0].cid[5] == 'a' ? 'b' : 'a';
    CHECK(post::verify(tampered, 4) != post::Error::None);
}

// ── Holder hints over Delivery (sync.h: mask / mhere) ───────────────────────

#include "forumer_core/sync.h"

TEST(media_holder_hints_round_trip) {
    const std::string cid = "zDvZRwzm7J5w8iY3s2Kq4cW8vHTPrnBDqrsxyUe8D4ycSv2E";
    auto ask = sync::decode(sync::encodeMediaAsk(cid));
    CHECK(ask.message && ask.message->type == sync::MessageType::MediaAsk && ask.message->media.cid == cid);

    sync::MediaHint h{cid, "16Uiu2HAm4S1JYkuzDKLKQvwgAhZKs9otxXqt8SCGtB4hoJP1S397",
                      {"/ip4/192.168.1.175/tcp/30123", "/dns4/node.example.org/udp/4001/quic-v1"}};
    auto here = sync::decode(sync::encodeMediaHere(h));
    CHECK(here.message && here.message->type == sync::MessageType::MediaHere);
    if (!here.message) return;
    CHECK_EQ(here.message->media.peer, h.peer);
    CHECK(here.message->media.addrs == h.addrs);
}

TEST(media_holder_hints_refuse_what_we_would_not_dial) {
    auto bad = [](const std::string& json) {
        return !sync::decode(std::vector<uint8_t>(json.begin(), json.end())).message.has_value();
    };
    const std::string cid = "\"zDvZRwzm7J5w8iY3s2Kq4cW8vHTPrnBDqrsxyUe8D4ycSv2E\"";
    const std::string peer = "\"16Uiu2HAm4S1JYkuzDKLKQvwgAhZKs9otxXqt8SCGtB4hoJP1S397\"";
    CHECK(bad(R"({"v":1,"t":"mask","cid":"../etc"})"));
    CHECK(bad(R"({"v":1,"t":"mhere","cid":)" + cid + R"(,"peer":"short","addrs":["/ip4/1.2.3.4/tcp/1"]})"));
    CHECK(bad(R"({"v":1,"t":"mhere","cid":)" + cid + R"(,"peer":)" + peer + R"(,"addrs":[]})"));
    CHECK(bad(R"({"v":1,"t":"mhere","cid":)" + cid + R"(,"peer":)" + peer + R"(,"addrs":["/unix/tmp/sock"]})"));
    CHECK(bad(R"({"v":1,"t":"mhere","cid":)" + cid + R"(,"peer":)" + peer + R"(,"addrs":["/ip4/1.2.3.4/tcp/1 ; rm"]})"));
    CHECK(bad(R"({"v":1,"t":"mhere","cid":)" + cid + R"(,"peer":)" + peer +
              R"(,"addrs":["/ip4/1.1.1.1/tcp/1","/ip4/1.1.1.1/tcp/2","/ip4/1.1.1.1/tcp/3","/ip4/1.1.1.1/tcp/4",)"
              R"("/ip4/1.1.1.1/tcp/5","/ip4/1.1.1.1/tcp/6","/ip4/1.1.1.1/tcp/7","/ip4/1.1.1.1/tcp/8","/ip4/1.1.1.1/tcp/9"]})"));
}

// ── Previews over Delivery (media.h "Previews", sync.h "mprev") ─────────────

#include <filesystem>

#include "forumer_core/post_store.h"

TEST(media_previews_travel_in_signed_sealed_pieces) {
    identity::Account a = identity::Account::create("x");
    const auto full = *media::seal(fakeJpeg(200'000), "image/jpeg");
    const auto ref = refFor(full, "image/jpeg");
    const Bytes jpeg = fakeJpeg(90'000);

    auto blob = media::sealPreview(jpeg, ref);
    CHECK(blob.has_value());
    if (!blob) return;
    CHECK(std::string(blob->begin(), blob->end()).find("JFIF") == std::string::npos);
    const std::string postId = std::string(32, 'a');
    const auto& persona = a.currentPersona();
    const Bytes sig = persona.sign(media::previewMessage(postId, 1, *blob));

    // Split, send, reassemble.
    const auto wire = sync::encodePreview(postId, 1, *blob, sig);
    CHECK(wire.size() == 3);
    Bytes back;
    for (const auto& w : wire) {
        CHECK(w.size() <= sync::kMaxPreviewPayload);
        auto d = sync::decode(w);
        CHECK(d.message && d.message->type == sync::MessageType::Preview);
        if (!d.message) return;
        CHECK(d.message->preview.postId == postId && d.message->preview.index == 1 &&
              d.message->preview.parts == 3);
        back.insert(back.end(), d.message->preview.data.begin(), d.message->preview.data.end());
    }
    CHECK(back == *blob);
    CHECK(crypto::verify(persona.publicKey(), media::previewMessage(postId, 1, back), sig));
    // Bound to the post and the index: moving it elsewhere breaks the signature.
    CHECK(!crypto::verify(persona.publicKey(), media::previewMessage(std::string(32, 'b'), 1, back), sig));
    CHECK(!crypto::verify(persona.publicKey(), media::previewMessage(postId, 0, back), sig));

    auto opened = media::openPreview(back, ref);
    CHECK(opened.has_value() && *opened == jpeg);
    // Only for this image, and only with the forum key in a private forum.
    auto other = refFor(*media::seal(fakeJpeg(1234), "image/jpeg"), "image/jpeg");
    CHECK(!media::openPreview(back, other).has_value());
    auto f = *forums::create("Club");
    auto inForum = *media::sealPreview(jpeg, ref, &f.key);
    CHECK(!media::openPreview(inForum, ref).has_value());
    CHECK(media::openPreview(inForum, ref, &f.key).has_value());

    CHECK(!media::sealPreview(fakeJpeg(media::kMaxPreviewBytes + 1), ref).has_value());
    CHECK(!media::sealPreview(fakePng(1000), ref).has_value());   // previews are JPEG
}

TEST(media_preview_pieces_are_checked) {
    auto bad = [](const std::string& json) {
        return !sync::decode(std::vector<uint8_t>(json.begin(), json.end())).message.has_value();
    };
    const std::string sig = toBase64Url(Bytes(64, 1));
    const std::string id = "\"" + std::string(32, 'a') + "\"";
    auto msg = [&](const std::string& post, int i, int k, int n, const std::string& s, const std::string& data) {
        return R"({"v":1,"t":"mprev","post":)" + post + R"(,"i":)" + std::to_string(i) + R"(,"k":)" +
               std::to_string(k) + R"(,"n":)" + std::to_string(n) + R"(,"sig":")" + s + R"(","data":")" + data + "\"}";
    };
    const std::string data = toBase64Url(Bytes(10, 2));
    CHECK(!bad(msg(id, 0, 0, 1, sig, data)));
    CHECK(bad(msg("\"../x\"", 0, 0, 1, sig, data)));
    CHECK(bad(msg(id, 4, 0, 1, sig, data)));          // a post has 4 images at most
    CHECK(bad(msg(id, 0, 1, 1, sig, data)));          // part beyond the count
    CHECK(bad(msg(id, 0, 0, 5, sig, data)));          // too many parts
    CHECK(bad(msg(id, 0, 0, 1, toBase64Url(Bytes(10, 1)), data)));
    CHECK(bad(msg(id, 0, 0, 1, sig, toBase64Url(Bytes(sync::kPreviewPartBytes + 1, 2)))));
    // Other message types keep the small limit.
    std::string big = R"({"v":1,"t":"digest","since":1,"have":[],"pad":")" + std::string(30'000, 'x') + "\"}";
    CHECK(bad(big));
}

TEST(media_previews_are_kept_and_go_with_their_forum) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / ("forumer-pv-" + toHex(crypto::randomBytes(6)));
    fs::create_directories(dir);
    {
        auto store = PostStore::open(dir / "posts.sqlite3");
        auto f = *forums::create("Club");
        identity::Account a = identity::Account::create("x");
        post::Draft d;
        d.kind = post::Kind::Post;
        d.forum = f.id;
        d.title = "t";
        d.domains = {"general"};
        d.timestampMs = 1'800'000'000'000;
        auto p = *post::sign(d, a.currentPersona(), identity::Disclosure::Persona, "", 4);
        const std::string sealed = forums::sealStored(f, p.toJson());
        store->insert(p, 1, &sealed);
        CHECK(store->putPreview({p.id, 0, "zcid", Bytes(100, 3), Bytes(64, 4)}));
        CHECK(store->putPreview({p.id, 0, "zcid", Bytes(5, 9), Bytes(64, 4)}));   // first one stays
        auto got = store->preview(p.id, 0);
        CHECK(got && got->blob == Bytes(100, 3));
        CHECK_EQ(store->previewsOf("zcid").size(), size_t(1));
        store->removeForum(f.id);
        CHECK(!store->preview(p.id, 0).has_value());
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}