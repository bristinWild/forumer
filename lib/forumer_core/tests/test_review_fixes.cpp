// Regression tests for the findings in the 0.2.2 review
// (https://data.vpavlin.xyz/forumer-review-fresh/): each test names the
// finding it covers.

#include "test.h"

#include <sqlite3.h>
#include <sys/stat.h>

#include <chrono>
#include <filesystem>
#include <set>
#include <string>

#include "forumer_core/account_store.h"
#include "forumer_core/crypto.h"
#include "forumer_core/identity.h"
#include "forumer_core/mnemonic.h"
#include "forumer_core/post.h"
#include "forumer_core/post_store.h"
#include "forumer_core/sealed.h"

using namespace forumer;
using namespace forumer::identity;
namespace fs = std::filesystem;

namespace {

constexpr int kTestPow = 4;
const std::string kPassword = "correct horse battery";

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

AccountStore makeStore(const fs::path& dir) {
    return AccountStore(dir, crypto::interactivePasswordHashParams());
}

int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

post::Post topic(const Persona& by, Disclosure d, const std::string& title, int64_t ts,
                 const std::string& alias = "") {
    post::Draft draft;
    draft.kind = post::Kind::Post;
    draft.title = title;
    draft.domains = {"privacy"};
    draft.timestampMs = ts;
    return *post::sign(draft, by, d, alias, kTestPow);
}

post::Post reply(const Persona& by, const post::Post& to, int64_t ts) {
    post::Draft draft;
    draft.kind = post::Kind::Reply;
    draft.root = to.id;
    draft.parent = to.id;
    draft.body = "a reply";
    draft.timestampMs = ts;
    return *post::sign(draft, by, Disclosure::Persona, "", kTestPow);
}

// Every byte of a file (and its -wal / -shm companions), as one string.
std::string fileBytes(const fs::path& file) {
    std::string all;
    for (const char* suffix : {"", "-wal", "-shm"}) {
        fs::path f = file;
        f += suffix;
        if (auto text = readFile(f)) all += *text;
    }
    return all;
}

// Every byte of every file under a directory.
std::string treeBytes(const fs::path& dir) {
    std::string all;
    for (const auto& entry : fs::recursive_directory_iterator(dir))
        if (entry.is_regular_file())
            if (auto text = readFile(entry.path())) all += *text;
    return all;
}

unsigned mode(const fs::path& p) {
    struct stat st {};
    if (::stat(p.c_str(), &st) != 0) return 0;
    return st.st_mode & 0777;
}

} // namespace

//  F1: one-time keys, restore

TEST(f1_anonymous_keys_dont_use_the_persona_counter) {
    Account a = Account::create("Main");
    const uint64_t next = a.state().nextIndex;
    std::set<Bytes> keys;
    for (int i = 0; i < 20; ++i) keys.insert(a.personaForPost(Disclosure::Anonymous).publicKey());
    CHECK_EQ(keys.size(), size_t(20));
    CHECK_EQ(a.state().nextIndex, next);  // the counter didn't move
    // None of them is a counter persona a restore would hand out later.
    for (const auto& pk : personaKeys(a.masterSecret(), 0, 200)) CHECK(keys.count(pk) == 0);
}

TEST(f1_restore_on_a_fresh_device_never_reuses_an_anonymous_key) {
    // Reproduces the review's probe p04: post anonymously, restore the phrase
    // on a device with an empty log, post anonymously again.
    Account original = Account::create("Main");
    const Bytes first = original.personaForPost(Disclosure::Anonymous).publicKey();

    auto decoded = mnemonic::decode(mnemonic::encode(original.masterSecret()));
    CHECK(decoded.ok());
    auto restored = Account::restore(std::move(*decoded.secret), "Phone",
                                     [](const Bytes&) { return false; });  // empty log
    CHECK(restored.has_value());
    const Bytes again = restored->personaForPost(Disclosure::Anonymous).publicKey();
    CHECK(again != first);
}

TEST(f1_restore_is_pending_until_history_and_moves_to_the_newest_persona) {
    Account original = Account::create("Main");
    original.setPolicy(RotationPolicy::Manual);
    for (int i = 0; i < 5; ++i) original.rotate();  // now on persona 5
    const Bytes current = original.currentPersona().publicKey();

    auto restored = Account::restore(original.masterSecret(), "Phone", [](const Bytes&) { return false; });
    CHECK(restored.has_value());
    CHECK(restored->restorePending());
    CHECK(restored->currentPersona().publicKey() != current);  // nothing known yet

    restored->setPolicy(RotationPolicy::Manual);
    // History arrives: a post by persona 5 turns up.
    CHECK(restored->notePersonaUsed(5, true));
    CHECK(restored->currentPersona().publicKey() == current);
    CHECK_EQ(restored->state().nextIndex, uint64_t(6));
    CHECK(!restored->notePersonaUsed(2, true));  // older: no change
    CHECK(restored->currentPersona().publicKey() == current);

    // Without resumeAs (a post from another device, no restore pending) only
    // the counter moves.
    Account other = Account::create("x");
    other.setPolicy(RotationPolicy::Manual);
    CHECK(other.notePersonaUsed(9, false));
    CHECK_EQ(other.state().currentIndex, uint64_t(0));
    CHECK_EQ(other.state().nextIndex, uint64_t(10));

    restored->finishRestore();
    CHECK(!restored->restorePending());
    // ...and the flag survives a save/load round trip either way.
    auto state = AccountState::fromJson(restored->state().toJson());
    CHECK(state.has_value() && !state->restored);
}

TEST(f1_skip_known_never_hands_out_a_used_persona) {
    Account a = Account::create("Main");
    a.setPolicy(RotationPolicy::Auto);
    std::set<Bytes> used;
    for (const auto& pk : personaKeys(a.masterSecret(), 1, 4)) used.insert(pk);  // 1, 2, 3 seen
    CHECK(a.skipKnown([&](const Bytes& pk) { return used.count(pk) > 0; }));
    CHECK_EQ(a.state().nextIndex, uint64_t(4));
    CHECK(used.count(a.personaForPost().publicKey()) == 0);
}

TEST(f1_recovery_tolerates_long_gaps) {
    // Pressing "rotate" 200 times without posting used to defeat the gap limit of 50.
    Account a = Account::create("Main");
    const Bytes far = a.persona(250).publicKey();
    CHECK_EQ(recoverNextIndex(a.masterSecret(), [&](const Bytes& pk) { return pk == far; }),
             uint64_t(251));
}

//  F2: accounts on one device are isolated

TEST(f2_second_account_sees_none_of_the_first_accounts_own_posts) {
    TempDir dir;
    auto store = PostStore::open(dir.path / "posts.sqlite3");
    Account alice = Account::create("alice");
    Account bob = Account::create("bob");
    const int64_t t = nowMs();

    const auto anon = topic(alice.personaForPost(Disclosure::Anonymous), Disclosure::Anonymous, "anon", t);
    const auto signedTopic = topic(alice.currentPersona(), Disclosure::Persona, "mine", t + 1);
    store->insert(anon, t);
    store->insert(signedTopic, t);
    store->enqueue(anon.id, alice.state().id, t);
    store->enqueue(signedTopic.id, alice.state().id, t);
    store->insert(reply(bob.currentPersona(), signedTopic, t + 2), t);  // a reply to alice
    const OwnData aliceData = store->exportOwn(alice.state().id);
    CHECK_EQ(aliceData.outbox.size(), size_t(2));
    CHECK_EQ(store->inbox(alice.state().id).size(), size_t(1));

    // Alice locks, Bob unlocks (the app: forgetOwn, then import Bob's data).
    store->forgetOwn();
    store->importOwn(store->exportOwn(bob.state().id));
    CHECK(store->outbox().empty());
    CHECK(store->unsent().empty());
    CHECK_EQ(store->countOwn(bob.state().id, post::Kind::Post, 0), size_t(0));
    CHECK(store->inbox(bob.state().id).empty());
    CHECK(!store->outboxEntry(anon.id).has_value());

    // Alice again: everything is back.
    store->forgetOwn();
    store->importOwn(aliceData);
    CHECK_EQ(store->outbox().size(), size_t(2));
    CHECK_EQ(store->inbox(alice.state().id).size(), size_t(1));
}

TEST(f2_own_data_round_trips_through_json) {
    OwnData data;
    data.accountId = "0123456789abcdef";
    OutboxEntry e;
    e.postId = "p1";
    e.state = SendState::Failed;
    e.attempts = 3;
    e.createdMs = 10;
    e.lastAttemptMs = 20;
    e.lastError = "no peers";
    data.outbox.push_back(e);
    data.read.emplace_back("r1", 30);

    auto back = OwnData::fromJson(data.toJson(), data.accountId);
    CHECK(back.has_value());
    CHECK_EQ(back->outbox.size(), size_t(1));
    CHECK(back->outbox[0].state == SendState::Failed);
    CHECK_EQ(back->outbox[0].attempts, 3);
    CHECK_EQ(back->outbox[0].lastError, std::string("no peers"));
    CHECK_EQ(back->read.size(), size_t(1));
    CHECK(OwnData::fromJson("", "x").has_value());          // no file yet: empty
    CHECK(!OwnData::fromJson("{bad", "x").has_value());
}

//  F3: nothing on disk links posts to an account

TEST(f3_post_log_file_holds_no_account_link) {
    TempDir dir;
    const fs::path file = dir.path / "posts" / "posts.sqlite3";
    Account a = Account::create("alice");
    const auto anon = topic(a.personaForPost(Disclosure::Anonymous), Disclosure::Anonymous, "anon", nowMs());
    {
        auto store = PostStore::open(file);
        store->insert(anon, 1);
        store->enqueue(anon.id, a.state().id, 1);
        store->markRead(a.state().id, {anon.id}, 2);
    }
    CHECK(fileBytes(file).find(a.state().id) == std::string::npos);
}

TEST(f3_post_log_is_owner_only) {
    TempDir dir;
    const fs::path file = dir.path / "posts" / "posts.sqlite3";
    {
        auto store = PostStore::open(file);
        CHECK(store != nullptr);
    }
    CHECK_EQ(mode(file), 0600u);
    CHECK_EQ(mode(file.parent_path()), 0700u);
}

TEST(f3_account_state_is_encrypted_at_rest) {
    TempDir dir;
    AccountStore store = makeStore(dir.path);
    std::string phrase;
    auto created = store.create("Campus", kPassword, phrase);
    CHECK(created.ok());
    Account& account = *created.account;
    account.setAlias("night owl");
    account.follow("very-secret-domain");
    account.rotate();
    CHECK(store.save(account));

    const fs::path accountDir = dir.path / account.state().id;
    CHECK(!fs::exists(accountDir / "state.json"));
    CHECK(fs::exists(accountDir / "state.enc"));
    const std::string all = treeBytes(dir.path);
    CHECK(all.find("night owl") == std::string::npos);
    CHECK(all.find("very-secret-domain") == std::string::npos);
    CHECK_EQ(mode(accountDir / "state.enc"), 0600u);
    CHECK_EQ(mode(accountDir), 0700u);

    // The lock screen still knows the name; unlocking brings the rest back.
    CHECK_EQ(store.list().size(), size_t(1));
    CHECK_EQ(store.list()[0].label, std::string("Campus"));
    auto unlocked = store.unlock(account.state().id, kPassword);
    CHECK(unlocked.ok());
    CHECK_EQ(unlocked.account->state().alias, std::string("night owl"));
    CHECK(unlocked.account->isFollowing("very-secret-domain"));
    CHECK_EQ(unlocked.account->state().currentIndex, uint64_t(1));
}

TEST(f3_legacy_clear_state_is_migrated_on_unlock) {
    TempDir dir;
    std::string id;
    {
        AccountStore store = makeStore(dir.path);
        std::string phrase;
        auto created = store.create("Old", kPassword, phrase);
        created.account->setAlias("legacy alias");
        id = created.account->state().id;
        // Recreate what 0.2.2 left on disk: state in clear, index without labels.
        const fs::path accountDir = dir.path / id;
        fs::remove(accountDir / "state.enc");
        writeFileAtomic(accountDir / "state.json", created.account->state().toJson());
        writeFileAtomic(dir.path / "accounts.json",
                        "{\"v\":1,\"selected\":\"" + id + "\",\"order\":[\"" + id + "\"]}");
    }
    AccountStore store = makeStore(dir.path);
    CHECK_EQ(store.list().size(), size_t(1));
    CHECK_EQ(store.list()[0].label, std::string("Old"));  // read from the old file
    auto unlocked = store.unlock(id, kPassword);
    CHECK(unlocked.ok());
    CHECK_EQ(unlocked.account->state().alias, std::string("legacy alias"));
    CHECK(!fs::exists(dir.path / id / "state.json"));
    CHECK(fs::exists(dir.path / id / "state.enc"));
    CHECK(treeBytes(dir.path).find("legacy alias") == std::string::npos);
    CHECK_EQ(store.list()[0].label, std::string("Old"));  // now from the index
}

TEST(f3_private_files_belong_to_one_account) {
    TempDir dir;
    AccountStore store = makeStore(dir.path);
    std::string phrase;
    auto a = store.create("A", kPassword, phrase);
    auto b = store.create("B", kPassword, phrase);
    CHECK_EQ(*store.loadPrivate(*a.account, "own"), std::string());  // none yet
    CHECK(store.savePrivate(*a.account, "own", "{\"hello\":1}"));
    CHECK_EQ(*store.loadPrivate(*a.account, "own"), std::string("{\"hello\":1}"));
    CHECK(treeBytes(dir.path).find("hello") == std::string::npos);

    // B's key can't open A's file, even copied into B's folder.
    fs::copy_file(dir.path / a.account->state().id / "own.enc",
                  dir.path / b.account->state().id / "own.enc");
    CHECK(!store.loadPrivate(*b.account, "own").has_value());
    CHECK(!store.savePrivate(*a.account, "../x", "no"));
    CHECK(!store.savePrivate(*a.account, "state", "no"));  // reserved
}

TEST(f3_legacy_outbox_rows_move_out_of_the_log) {
    TempDir dir;
    const fs::path file = dir.path / "posts.sqlite3";
    Account a = Account::create("A");
    const auto t = topic(a.currentPersona(), Disclosure::Persona, "old", nowMs());
    {
        // A log as 0.2.2 wrote it, with the clear outbox and read tables.
        auto store = PostStore::open(file);
        store->insert(t, 1);
    }
    {
        sqlite3* db = nullptr;
        CHECK(sqlite3_open(file.string().c_str(), &db) == SQLITE_OK);
        const std::string sql =
            "CREATE TABLE outbox (post_id TEXT PRIMARY KEY, account_id TEXT NOT NULL, state TEXT NOT NULL,"
            " attempts INTEGER NOT NULL DEFAULT 0, created INTEGER NOT NULL,"
            " last_attempt INTEGER NOT NULL DEFAULT 0, last_error TEXT NOT NULL DEFAULT '');"
            "CREATE TABLE inbox_read (account_id TEXT NOT NULL, post_id TEXT NOT NULL,"
            " read_at INTEGER NOT NULL, PRIMARY KEY (account_id, post_id));"
            "INSERT INTO outbox(post_id, account_id, state, created) VALUES ('" + t.id + "', '" +
            a.state().id + "', 'failed', 5);"
            "INSERT INTO inbox_read VALUES ('" + a.state().id + "', 'r1', 6);";
        CHECK(sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK);
        sqlite3_close(db);
    }
    auto store = PostStore::open(file);
    CHECK(store->legacyRowsLeft());
    OwnData legacy = store->takeLegacy(a.state().id);
    CHECK_EQ(legacy.outbox.size(), size_t(1));
    CHECK(legacy.outbox[0].state == SendState::Failed);
    CHECK_EQ(legacy.read.size(), size_t(1));
    CHECK(!store->legacyRowsLeft());
    store->dropLegacyTablesIfEmpty();
    store.reset();
    CHECK(fileBytes(file).find(a.state().id) == std::string::npos);
}

TEST(f3_sealed_binds_account_and_purpose) {
    const SecretBytes key = crypto::randomSecret(32);
    auto text = sealed::seal(key, "acct", "own", "secret");
    CHECK(text.has_value());
    CHECK(sealed::open(key, "acct", "own", *text) == std::optional<std::string>("secret"));
    CHECK(!sealed::open(key, "other", "own", *text).has_value());
    CHECK(!sealed::open(key, "acct", "state", *text).has_value());
    CHECK(!sealed::open(crypto::randomSecret(32), "acct", "own", *text).has_value());
}