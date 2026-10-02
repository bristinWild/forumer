#include "test.h"

#include <nlohmann/json.hpp>

#include "forumer_core/crypto.h"
#include "forumer_core/vault.h"

using namespace forumer;

namespace {
// Tests use the faster Argon2id preset; the app uses the default (moderate).
const crypto::PasswordHashParams kFast = crypto::interactivePasswordHashParams();
} // namespace

TEST(vault_round_trip) {
    const SecretBytes master = crypto::randomSecret(32);
    auto vaultJson = vault::seal(master, "hunter2 is not a good password", kFast);
    CHECK(vaultJson.has_value());

    auto opened = vault::open(*vaultJson, "hunter2 is not a good password");
    CHECK(opened.ok());
    CHECK(opened.error == vault::OpenError::None);
    CHECK(opened.secret->equals(master));
}

TEST(vault_rejects_wrong_password) {
    const SecretBytes master = crypto::randomSecret(32);
    auto vaultJson = vault::seal(master, "right password", kFast);
    auto opened = vault::open(*vaultJson, "wrong password");
    CHECK(!opened.ok());
    CHECK(opened.error == vault::OpenError::WrongPassword);
}

TEST(vault_rejects_empty_password_on_seal) {
    CHECK(!vault::seal(crypto::randomSecret(32), "", kFast).has_value());
}

TEST(vault_never_contains_the_secret_in_clear) {
    const SecretBytes master = crypto::randomSecret(32);
    auto vaultJson = vault::seal(master, "pw", kFast);
    const std::string asHex = toHex(master.data(), master.size());
    const std::string asB64 = toBase64Url(master.data(), master.size());
    CHECK(vaultJson->find(asHex) == std::string::npos);
    CHECK(vaultJson->find(asB64) == std::string::npos);
}

TEST(vault_is_randomised_per_seal) {
    const SecretBytes master = crypto::randomSecret(32);
    auto a = vault::seal(master, "pw", kFast);
    auto b = vault::seal(master, "pw", kFast);
    CHECK(*a != *b);  // fresh salt and nonce each time
}

TEST(vault_uses_cost_stored_in_the_file) {
    // Sealed with the fast preset; open() takes no params and must read them
    // back from the vault rather than assuming the current default.
    auto vaultJson = vault::seal(crypto::randomSecret(32), "pw", kFast);
    const auto doc = nlohmann::json::parse(*vaultJson);
    CHECK_EQ(doc["ops"].get<uint64_t>(), kFast.opsLimit);
    CHECK_EQ(doc["mem"].get<uint64_t>(), kFast.memLimit);
    CHECK(vault::open(*vaultJson, "pw").ok());
}

TEST(vault_detects_tampering) {
    auto vaultJson = vault::seal(crypto::randomSecret(32), "pw", kFast);
    auto doc = nlohmann::json::parse(*vaultJson);

    // Flip one byte of the ciphertext.
    Bytes ct = *fromBase64Url(doc["ct"].get<std::string>());
    ct[0] ^= 1;
    doc["ct"] = toBase64Url(ct);
    CHECK(vault::open(doc.dump(), "pw").error == vault::OpenError::WrongPassword);
}

TEST(vault_reports_malformed_and_unsupported) {
    CHECK(vault::open("not json", "pw").error == vault::OpenError::Malformed);
    CHECK(vault::open("{}", "pw").error == vault::OpenError::Malformed);

    auto vaultJson = vault::seal(crypto::randomSecret(32), "pw", kFast);
    auto doc = nlohmann::json::parse(*vaultJson);

    auto newer = doc;
    newer["v"] = 99;
    CHECK(vault::open(newer.dump(), "pw").error == vault::OpenError::UnsupportedVersion);

    auto badSalt = doc;
    badSalt["salt"] = "AAAA";  // wrong length
    CHECK(vault::open(badSalt.dump(), "pw").error == vault::OpenError::Malformed);
}

TEST(vault_change_password_keeps_the_same_secret) {
    const SecretBytes master = crypto::randomSecret(32);
    auto original = vault::seal(master, "old", kFast);

    auto changed = vault::changePassword(*original, "old", "new", kFast);
    CHECK(changed.vaultJson.has_value());
    CHECK(vault::open(*changed.vaultJson, "old").error == vault::OpenError::WrongPassword);
    auto reopened = vault::open(*changed.vaultJson, "new");
    CHECK(reopened.ok() && reopened.secret->equals(master));

    auto refused = vault::changePassword(*original, "not old", "new", kFast);
    CHECK(!refused.vaultJson.has_value());
    CHECK(refused.error == vault::OpenError::WrongPassword);
}