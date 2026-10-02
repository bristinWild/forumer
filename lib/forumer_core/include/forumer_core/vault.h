#pragma once

// Password vault: keeps the master secret encrypted at rest.
//
//   password --Argon2id(salt, cost)--> 32-byte key --XChaCha20-Poly1305--> ciphertext
//
// The vault is a small JSON document (text in, text out — writing it to disk
// is the caller's job):
//
//   { "v": 1, "kdf": "argon2id13", "ops": 3, "mem": 268435456,
//     "salt": "<b64url>", "nonce": "<b64url>", "ct": "<b64url>" }
//
// Nothing secret is stored in clear. The cost settings travel with each vault,
// so raising the defaults later never locks out an existing vault. The format
// version is also bound into the ciphertext as associated data, so a vault
// can't be replayed under a different version label.

#include <optional>
#include <string>
#include <string_view>

#include "forumer_core/bytes.h"
#include "forumer_core/crypto.h"

namespace forumer::vault {

inline constexpr int kFormatVersion = 1;

enum class OpenError {
    None,
    Malformed,           // not valid vault JSON / fields missing or badly encoded
    UnsupportedVersion,  // written by a newer Forumer
    WrongPassword,       // authentication failed: wrong password or tampered file
    KdfFailed,           // Argon2id could not run (e.g. out of memory)
};

struct OpenResult {
    std::optional<SecretBytes> secret;  // set on success
    OpenError error = OpenError::None;

    bool ok() const { return secret.has_value(); }
};

/// Encrypt `secret` under `password`. Returns the vault JSON, or nullopt if the
/// password is empty or key derivation fails. A fresh salt and nonce are drawn
/// every call, so sealing the same secret twice gives different vaults.
std::optional<std::string> seal(const SecretBytes& secret, std::string_view password,
                                const crypto::PasswordHashParams& params =
                                    crypto::defaultPasswordHashParams());

/// Decrypt a vault. Uses the cost settings stored in the vault itself.
OpenResult open(std::string_view vaultJson, std::string_view password);

/// Change the password: open with `oldPassword`, re-seal the same secret under
/// `newPassword` (new salt, new nonce, current default cost). On failure the
/// error says why and no vault is returned.
struct ResealResult {
    std::optional<std::string> vaultJson;
    OpenError error = OpenError::None;
};
ResealResult changePassword(std::string_view vaultJson, std::string_view oldPassword,
                            std::string_view newPassword,
                            const crypto::PasswordHashParams& params =
                                crypto::defaultPasswordHashParams());

/// Human-readable message for an error (for logs and UI).
const char* describe(OpenError error);

} // namespace forumer::vault