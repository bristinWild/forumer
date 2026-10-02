#pragma once

// Thin, typed wrappers over libsodium — the only cryptography Forumer uses.
//
// We never implement primitives ourselves. Each wrapper fixes one algorithm
// and its sizes so the rest of the code can't mix them up:
//
//   Signatures        Ed25519                       (posts, replies, DMs)
//   Hashing           BLAKE2b                       (message ids, PoW, topic ids)
//   Key derivation    BLAKE2b-based crypto_kdf      (master secret -> personas)
//   Password hashing  Argon2id                      (password -> vault key)
//   Encryption        XChaCha20-Poly1305 (AEAD)     (vault, private forums)
//
// Call initCrypto() once before anything else (it is idempotent).

#include <cstdint>
#include <optional>
#include <string_view>

#include "forumer_core/bytes.h"

namespace forumer::crypto {

//Sizes

inline constexpr size_t kSeedBytes = 32;          // Ed25519 seed / master secret / symmetric key
inline constexpr size_t kPublicKeyBytes = 32;     // Ed25519 public key
inline constexpr size_t kSecretKeyBytes = 64;     // Ed25519 secret key (seed || public key)
inline constexpr size_t kSignatureBytes = 64;     // Ed25519 signature
inline constexpr size_t kAeadKeyBytes = 32;       // XChaCha20-Poly1305 key
inline constexpr size_t kAeadNonceBytes = 24;     // XChaCha20-Poly1305 nonce
inline constexpr size_t kAeadTagBytes = 16;       // Poly1305 tag appended to ciphertext
inline constexpr size_t kPwSaltBytes = 16;        // Argon2id salt
inline constexpr size_t kKdfContextBytes = 8;     // crypto_kdf context string length

// Setup & randomness 

/// Initialise libsodium. Safe to call repeatedly; returns false only if the
/// library cannot be initialised (no usable entropy source).
bool initCrypto();

/// Cryptographically secure random bytes.
Bytes randomBytes(size_t size);
SecretBytes randomSecret(size_t size);

// Ed25519 signatures

struct SigningKeyPair {
    Bytes publicKey;        // kPublicKeyBytes
    SecretBytes secretKey;  // kSecretKeyBytes
};

/// Deterministic key pair from a 32-byte seed. The same seed always yields the
/// same key pair — this is what lets personas be re-derived from the master
/// secret instead of being stored.
std::optional<SigningKeyPair> signingKeyPairFromSeed(const SecretBytes& seed);

/// Detached signature over `message`.
Bytes sign(const SecretBytes& secretKey, const Bytes& message);

/// True only if `signature` is a valid signature of `message` by `publicKey`.
/// Wrong-sized inputs simply fail verification.
bool verify(const Bytes& publicKey, const Bytes& message, const Bytes& signature);

//  Hashing

/// BLAKE2b with an output length between 16 and 64 bytes (default 32).
Bytes blake2b(const Bytes& data, size_t outLen = 32);

//  Key derivation

/// Derive an independent subkey from a 32-byte root key. `context` must be
/// exactly 8 characters and names the purpose (e.g. "frmrpers" for personas);
/// different contexts or ids give unrelated keys, and no subkey reveals the
/// root or any sibling. Returns nullopt on bad sizes.
std::optional<SecretBytes> deriveSubkey(const SecretBytes& rootKey, uint64_t subkeyId,
                                        std::string_view context, size_t outLen = 32);

// Password hashing (Argon2id)

/// Cost settings for Argon2id. Stored next to the salt so a vault created with
/// one setting can still be opened after defaults change.
struct PasswordHashParams {
    uint64_t opsLimit;
    uint64_t memLimit; // bytes
};

/// libsodium's MODERATE preset: ~256 MiB, well under a second on a laptop.
/// Strong enough for an on-disk vault without making unlock painful.
PasswordHashParams defaultPasswordHashParams();

/// Faster, weaker preset (64 MiB). For unit tests only.
PasswordHashParams interactivePasswordHashParams();

/// Stretch a password into a 32-byte key. Returns nullopt on bad salt size or
/// if Argon2id fails (e.g. not enough memory).
std::optional<SecretBytes> deriveKeyFromPassword(std::string_view password, const Bytes& salt,
                                                 const PasswordHashParams& params);

//  Authenticated encryption (XChaCha20-Poly1305) 

struct Sealed {
    Bytes nonce;       // kAeadNonceBytes, random per message
    Bytes ciphertext;  // plaintext length + kAeadTagBytes
};

/// Encrypt and authenticate `plaintext`. `associatedData` is authenticated but
/// not encrypted (bind context such as a version or forum id to the
/// ciphertext). A fresh random nonce is generated every call.
std::optional<Sealed> aeadEncrypt(const SecretBytes& key, const uint8_t* plaintext, size_t size,
                                  const Bytes& associatedData = {});

/// Decrypt; nullopt if the key, nonce, ciphertext or associated data don't
/// match (tampering or wrong key). Result is secret-typed so callers decrypting
/// key material get automatic wiping.
std::optional<SecretBytes> aeadDecrypt(const SecretBytes& key, const Sealed& sealed,
                                       const Bytes& associatedData = {});

} // namespace forumer::crypto