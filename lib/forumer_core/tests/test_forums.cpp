#include "test.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>

#include "forumer_core/crypto.h"
#include "forumer_core/forums.h"
#include "forumer_core/identity.h"
#include "forumer_core/post.h"
#include "forumer_core/post_store.h"
#include "forumer_core/sync.h"

using namespace forumer;
namespace fs = std::filesystem;

namespace {

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

std::optional<std::string> readFileBytes(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return std::nullopt;
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

post::Post forumTopic(const identity::Persona& by, const std::string& forum, const std::string& title,
                      int64_t ts) {
    post::Draft d;
    d.kind = post::Kind::Post;
    d.forum = forum;
    d.title = title;
    d.body = "secret body of " + title;
    d.domains = {"general"};
    d.timestampMs = ts;
    return *post::sign(d, by, identity::Disclosure::Persona, "", 4);
}

} // namespace

TEST(forums_create_and_rebuild_from_the_key) {
    auto f = forums::create("Campus council");
    CHECK(f.has_value());
    if (!f) return;
    CHECK_EQ(f->id.size(), size_t(16));
    CHECK_EQ(f->id, forums::idFor(f->key));
    CHECK(!sync::contentTopic(forums::topicName(f->id)).empty());

    // The invite carries the key; the id and everything else follow from it.
    const std::string text = forums::keyToText(f->key);
    auto key = forums::keyFromText(text);
    CHECK(key.has_value());
    auto joined = forums::fromKey(*key, "Campus council");
    CHECK(joined.has_value() && joined->id == f->id);

    CHECK(!forums::keyFromText("not a key").has_value());
    CHECK(!forums::keyFromText(forums::keyToText(crypto::randomSecret(16))).has_value());
    CHECK(forums::create("Another").value().id != f->id);
}

TEST(forums_names_are_checked) {
    CHECK(forums::validName("Campus council"));
    CHECK(forums::validName("सभा"));
    CHECK(!forums::validName(""));
    CHECK(!forums::validName(" padded"));
    CHECK(!forums::validName("line\nbreak"));
    CHECK(!forums::validName(std::string(65, 'a')));
    CHECK(!forums::validName("\xFF\xFE"));
}

TEST(forums_wire_messages_are_sealed_and_bound_to_the_forum) {
    auto a = *forums::create("A");
    auto b = *forums::create("B");
    identity::Account who = identity::Account::create("x");
    const auto p = forumTopic(who.currentPersona(), a.id, "plans", nowMs());
    const auto inner = sync::encodePost(p);

    const auto sealed = forums::sealWire(a, inner);
    const std::string text(sealed.begin(), sealed.end());
    CHECK(text.find("plans") == std::string::npos);          // nothing readable on the wire
    CHECK(text.find("secret body") == std::string::npos);

    auto opened = forums::openWire(a, sealed);
    CHECK(opened.has_value() && *opened == inner);
    CHECK(!forums::openWire(b, sealed).has_value());          // another forum's key
    auto tampered = sealed;
    tampered[tampered.size() / 2] ^= 1;
    CHECK(!forums::openWire(a, tampered).has_value());
    CHECK(!forums::openWire(a, inner).has_value());           // not sealed at all
}

TEST(forums_posts_are_sealed_at_rest_and_kept_apart_from_public_sync) {
    TempDir dir;
    const fs::path file = dir.path / "posts.sqlite3";
    auto f = *forums::create("Club");
    identity::Account who = identity::Account::create("x");
    const int64_t t = nowMs() - 10'000;
    const auto priv = forumTopic(who.currentPersona(), f.id, "members only", t);
    const auto pub = forumTopic(who.persona(1), "public", "open topic", t);
    {
        auto store = PostStore::open(file);
        const std::string sealed = forums::sealStored(f, priv.toJson());
        CHECK(forums::isSealedStored(sealed));
        CHECK(store->insert(priv, t, &sealed) == PostStore::Insert::Added);
        CHECK(store->insert(pub, t) == PostStore::Insert::Added);

        // Public digests never list private posts, and the other way round.
        CHECK_EQ(store->recent(0).size(), size_t(1));
        CHECK_EQ(store->recent(0, f.id).size(), size_t(1));
        CHECK_EQ(store->recent(0, f.id)[0].id, priv.id);

        // Only the forum's key opens what's stored.
        auto row = store->stored(priv.id);
        CHECK(row.has_value() && row->forum == f.id);
        auto json = forums::openStored(f, row->envelope);
        CHECK(json.has_value());
        auto back = post::Post::fromJson(*json);
        CHECK(back.has_value() && back->id == priv.id);
        CHECK(!forums::openStored(*forums::create("Other"), row->envelope).has_value());

        auto page = store->page(0, 10);
        CHECK_EQ(page.posts.size(), size_t(1));
        CHECK_EQ(page.sealed.size(), size_t(1));
    }
    // The file never holds the private text.
    std::string bytes;
    for (const char* suffix : {"", "-wal"}) {
        fs::path p = file;
        p += suffix;
        if (auto t2 = readFileBytes(p)) bytes += *t2;
    }
    CHECK(bytes.find("members only") == std::string::npos);
    CHECK(bytes.find("secret body of members only") == std::string::npos);
    CHECK(bytes.find("open topic") != std::string::npos);
}

TEST(forums_leaving_drops_the_forums_posts) {
    TempDir dir;
    auto store = PostStore::open(dir.path / "posts.sqlite3");
    auto f = *forums::create("Club");
    identity::Account who = identity::Account::create("x");
    const auto priv = forumTopic(who.currentPersona(), f.id, "x", nowMs());
    const std::string sealed = forums::sealStored(f, priv.toJson());
    store->insert(priv, 1, &sealed);
    CHECK(store->enqueue(priv.id, "acct", 1));
    CHECK_EQ(store->unsent().size(), size_t(1));
    CHECK_EQ(store->removeForum(f.id), size_t(1));
    CHECK(!store->contains(priv.id));
    CHECK(store->unsent().empty());               // nothing left to retry
    CHECK(store->exportOwn("acct").outbox.empty());
}

TEST(forums_memberships_live_in_account_state) {
    identity::Account a = identity::Account::create("A");
    auto f = *forums::create("Club");
    CHECK(a.joinForum({f.id, forums::keyToText(f.key), f.name, 5}));
    CHECK(a.joinForum({f.id, forums::keyToText(f.key), "Renamed", 6}));   // same forum: renamed
    CHECK_EQ(a.state().forums.size(), size_t(1));
    auto state = identity::AccountState::fromJson(a.state().toJson());
    CHECK(state.has_value() && state->forums.size() == 1 && state->forums[0].name == "Renamed");
    a.leaveForum(f.id);
    CHECK(a.state().forums.empty());
    CHECK(a.forum(f.id) == nullptr);
}