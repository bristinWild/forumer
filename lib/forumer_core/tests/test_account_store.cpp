#include "test.h"

#include <filesystem>
#include <set>

#include "forumer_core/account_store.h"
#include "forumer_core/crypto.h"
#include "forumer_core/mnemonic.h"

using namespace forumer;
namespace fs = std::filesystem;

namespace {

// A fresh, empty directory per test, removed when the test ends.
struct TempDir {
    fs::path path;
    TempDir() {
        path = fs::temp_directory_path() /
               ("forumer-test-" + toHex(crypto::randomBytes(6)));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

AccountStore makeStore(const TempDir& dir) {
    return AccountStore(dir.path, crypto::interactivePasswordHashParams());  // fast for tests
}

const std::string kPassword = "correct horse battery";

} // namespace

TEST(store_starts_empty) {
    TempDir dir;
    AccountStore store = makeStore(dir);
    CHECK(store.empty());
    CHECK(!store.selectedId().has_value());
}

TEST(store_create_then_unlock) {
    TempDir dir;
    AccountStore store = makeStore(dir);
    std::string phrase;
    auto created = store.create("Main", kPassword, phrase);
    CHECK(created.ok());
    CHECK_EQ(mnemonic::decode(phrase).ok(), true);

    const std::string id = created.account->state().id;
    CHECK(store.selectedId() == id);
    CHECK_EQ(store.list().size(), size_t(1));
    CHECK_EQ(store.list()[0].label, std::string("Main"));

    auto unlocked = store.unlock(id, kPassword);
    CHECK(unlocked.ok());
    CHECK(unlocked.account->masterSecret().equals(created.account->masterSecret()));

    auto wrong = store.unlock(id, "not the password");
    CHECK(!wrong.ok());
    CHECK_EQ(wrong.error, std::string("wrong password"));
}

TEST(store_rejects_short_passwords) {
    TempDir dir;
    AccountStore store = makeStore(dir);
    std::string phrase;
    auto created = store.create("Main", "short", phrase);
    CHECK(!created.ok());
    CHECK(phrase.empty());
    CHECK(store.empty());
}

TEST(store_vault_is_owner_only_and_secret_free) {
    TempDir dir;
    AccountStore store = makeStore(dir);
    std::string phrase;
    auto created = store.create("Main", kPassword, phrase);
    const fs::path vault = dir.path / created.account->state().id / "vault.json";

    const auto perms = fs::status(vault).permissions();
    CHECK((perms & (fs::perms::group_all | fs::perms::others_all)) == fs::perms::none);

    const auto& m = created.account->masterSecret();
    const std::string contents = *readFile(vault);
    CHECK(contents.find(toHex(m.data(), m.size())) == std::string::npos);
    CHECK(contents.find(toBase64Url(m.data(), m.size())) == std::string::npos);
    CHECK(!fs::exists(dir.path / created.account->state().id / "vault.json.tmp"));
}

TEST(store_save_persists_state_changes) {
    TempDir dir;
    AccountStore store = makeStore(dir);
    std::string phrase;
    auto created = store.create("Main", kPassword, phrase);
    identity::Account& account = *created.account;
    account.setPolicy(identity::RotationPolicy::Auto);
    account.personaForPost();
    account.personaForPost();
    CHECK(store.save(account));

    auto reopened = store.unlock(account.state().id, kPassword);
    CHECK(reopened.ok());
    CHECK(reopened.account->state().policy == identity::RotationPolicy::Auto);
    CHECK_EQ(reopened.account->state().nextIndex, account.state().nextIndex);
}

TEST(store_multiple_accounts_and_selection) {
    TempDir dir;
    AccountStore store = makeStore(dir);
    std::string p1, p2;
    auto a = store.create("Main", kPassword, p1);
    auto b = store.create("Campus", kPassword, p2);
    CHECK_EQ(store.list().size(), size_t(2));
    CHECK(store.selectedId() == b.account->state().id);  // newest is selected

    CHECK(store.select(a.account->state().id));
    CHECK(store.selectedId() == a.account->state().id);
    CHECK(!store.select("0000000000000000"));  // not an account here
    CHECK(!store.select("../../etc"));         // never a path
}

TEST(store_restore_on_new_device_recovers_counter) {
    TempDir oldDevice, newDevice;
    AccountStore oldStore = makeStore(oldDevice);
    std::string phrase;
    auto created = oldStore.create("Main", kPassword, phrase);
    identity::Account& original = *created.account;
    original.setPolicy(identity::RotationPolicy::Auto);
    std::set<Bytes> published;
    for (int i = 0; i < 4; ++i) published.insert(original.personaForPost().publicKey());

    AccountStore newStore = makeStore(newDevice);
    auto restored = newStore.restore(phrase, "Phone", "a brand new password",
                                     [&](const Bytes& pk) { return published.count(pk) > 0; });
    CHECK(restored.ok());
    CHECK_EQ(restored.account->state().id, original.state().id);
    CHECK_EQ(restored.account->state().nextIndex, original.state().nextIndex);
    CHECK(newStore.unlock(original.state().id, "a brand new password").ok());
}

TEST(store_restore_existing_account_keeps_settings_and_resets_password) {
    TempDir dir;
    AccountStore store = makeStore(dir);
    std::string phrase;
    auto created = store.create("Main", kPassword, phrase);
    created.account->setAlias("night owl");
    store.save(*created.account);

    // Forgot the password: restore from the phrase with a new one.
    auto restored = store.restore(phrase, "ignored", "my new password",
                                  [](const Bytes&) { return false; });
    CHECK(restored.ok());
    CHECK_EQ(restored.account->state().alias, std::string("night owl"));
    CHECK_EQ(store.list().size(), size_t(1));  // not duplicated
    CHECK(!store.unlock(restored.account->state().id, kPassword).ok());
    CHECK(store.unlock(restored.account->state().id, "my new password").ok());
}

TEST(store_restore_reports_bad_phrase) {
    TempDir dir;
    AccountStore store = makeStore(dir);
    auto restored = store.restore("not a real phrase", "x", kPassword,
                                  [](const Bytes&) { return false; });
    CHECK(!restored.ok());
    CHECK(!restored.error.empty());
    CHECK(store.empty());
}

TEST(store_change_password) {
    TempDir dir;
    AccountStore store = makeStore(dir);
    std::string phrase;
    auto created = store.create("Main", kPassword, phrase);
    const std::string id = created.account->state().id;

    CHECK_EQ(store.changePassword(id, "wrong password", "another password"),
             std::string("wrong password"));
    CHECK(store.changePassword(id, kPassword, "another password").empty());
    CHECK(!store.unlock(id, kPassword).ok());
    CHECK(store.unlock(id, "another password").ok());
}

TEST(store_remove_account) {
    TempDir dir;
    AccountStore store = makeStore(dir);
    std::string p1, p2;
    auto a = store.create("Main", kPassword, p1);
    auto b = store.create("Campus", kPassword, p2);

    CHECK(store.remove(b.account->state().id));
    CHECK_EQ(store.list().size(), size_t(1));
    CHECK(store.selectedId() == a.account->state().id);
    CHECK(!fs::exists(dir.path / b.account->state().id));
}