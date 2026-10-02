#include "forumer_core/crypto.h"

#include <sodium.h>

static_assert(forumer::crypto::kSeedBytes == crypto_sign_SEEDBYTES);
static_assert(forumer::crypto::kPublicKeyBytes == crypto_sign_PUBLICKEYBYTES);
static_assert(forumer::crypto::kSecretKeyBytes == crypto_sign_SECRETKEYBYTES);
static_assert(forumer::crypto::kSignatureBytes == crypto_sign_BYTES);
static_assert(forumer::crypto::kAeadKeyBytes == crypto_aead_xchacha20poly1305_ietf_KEYBYTES);
static_assert(forumer::crypto::kAeadNonceBytes == crypto_aead_xchacha20poly1305_ietf_NPUBBYTES);
static_assert(forumer::crypto::kAeadTagBytes == crypto_aead_xchacha20poly1305_ietf_ABYTES);
static_assert(forumer::crypto::kPwSaltBytes == crypto_pwhash_SALTBYTES);
static_assert(forumer::crypto::kKdfContextBytes == crypto_kdf_CONTEXTBYTES);

namespace forumer::crypto {

//  Setup & randomness 

bool initCrypto() {
    // 0 = initialised now, 1 = already initialised, -1 = failure.
    return sodium_init() >= 0;
}

Bytes randomBytes(size_t size) {
    Bytes out(size);
    if (size > 0) randombytes_buf(out.data(), size);
    return out;
}

SecretBytes randomSecret(size_t size) {
    SecretBytes out(size);
    if (size > 0) randombytes_buf(out.data(), size);
    return out;
}

//  Ed25519 

std::optional<SigningKeyPair> signingKeyPairFromSeed(const SecretBytes& seed) {
    if (seed.size() != kSeedBytes) return std::nullopt;
    SigningKeyPair kp{Bytes(kPublicKeyBytes), SecretBytes(kSecretKeyBytes)};
    if (crypto_sign_seed_keypair(kp.publicKey.data(), kp.secretKey.data(), seed.data()) != 0)
        return std::nullopt;
    return kp;
}

Bytes sign(const SecretBytes& secretKey, const Bytes& message) {
    if (secretKey.size() != kSecretKeyBytes) return {};
    Bytes sig(kSignatureBytes);
    crypto_sign_detached(sig.data(), nullptr, message.data(), message.size(), secretKey.data());
    return sig;
}

bool verify(const Bytes& publicKey, const Bytes& message, const Bytes& signature) {
    if (publicKey.size() != kPublicKeyBytes || signature.size() != kSignatureBytes) return false;
    return crypto_sign_verify_detached(signature.data(), message.data(), message.size(),
                                       publicKey.data()) == 0;
}

//  Hashing 

Bytes blake2b(const Bytes& data, size_t outLen) {
    if (outLen < crypto_generichash_BYTES_MIN || outLen > crypto_generichash_BYTES_MAX) return {};
    Bytes out(outLen);
    crypto_generichash(out.data(), outLen, data.data(), data.size(), nullptr, 0);
    return out;
}

//  Key derivation 

std::optional<SecretBytes> deriveSubkey(const SecretBytes& rootKey, uint64_t subkeyId,
                                        std::string_view context, size_t outLen) {
    if (rootKey.size() != crypto_kdf_KEYBYTES) return std::nullopt;
    if (context.size() != kKdfContextBytes) return std::nullopt;
    if (outLen < crypto_kdf_BYTES_MIN || outLen > crypto_kdf_BYTES_MAX) return std::nullopt;

    char ctx[crypto_kdf_CONTEXTBYTES];
    for (size_t i = 0; i < crypto_kdf_CONTEXTBYTES; ++i) ctx[i] = context[i];

    SecretBytes out(outLen);
    if (crypto_kdf_derive_from_key(out.data(), outLen, subkeyId, ctx, rootKey.data()) != 0)
        return std::nullopt;
    return out;
}

// Password hashing 

PasswordHashParams defaultPasswordHashParams() {
    return {crypto_pwhash_OPSLIMIT_MODERATE, crypto_pwhash_MEMLIMIT_MODERATE};
}

PasswordHashParams interactivePasswordHashParams() {
    return {crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE};
}

std::optional<SecretBytes> deriveKeyFromPassword(std::string_view password, const Bytes& salt,
                                                 const PasswordHashParams& params) {
    if (salt.size() != kPwSaltBytes) return std::nullopt;
    SecretBytes key(kAeadKeyBytes);
    if (crypto_pwhash(key.data(), key.size(), password.data(), password.size(), salt.data(),
                      params.opsLimit, static_cast<size_t>(params.memLimit),
                      crypto_pwhash_ALG_ARGON2ID13) != 0)
        return std::nullopt;
    return key;
}

//  AEAD 

std::optional<Sealed> aeadEncrypt(const SecretBytes& key, const uint8_t* plaintext, size_t size,
                                  const Bytes& associatedData) {
    if (key.size() != kAeadKeyBytes) return std::nullopt;
    Sealed out{randomBytes(kAeadNonceBytes), Bytes(size + kAeadTagBytes)};
    unsigned long long written = 0;
    if (crypto_aead_xchacha20poly1305_ietf_encrypt(
            out.ciphertext.data(), &written, plaintext, size, associatedData.data(),
            associatedData.size(), nullptr, out.nonce.data(), key.data()) != 0)
        return std::nullopt;
    out.ciphertext.resize(written);
    return out;
}

std::optional<SecretBytes> aeadDecrypt(const SecretBytes& key, const Sealed& sealed,
                                       const Bytes& associatedData) {
    if (key.size() != kAeadKeyBytes || sealed.nonce.size() != kAeadNonceBytes) return std::nullopt;
    if (sealed.ciphertext.size() < kAeadTagBytes) return std::nullopt;

    SecretBytes out(sealed.ciphertext.size() - kAeadTagBytes);
    unsigned long long written = 0;
    if (crypto_aead_xchacha20poly1305_ietf_decrypt(
            out.data(), &written, nullptr, sealed.ciphertext.data(), sealed.ciphertext.size(),
            associatedData.data(), associatedData.size(), sealed.nonce.data(), key.data()) != 0)
        return std::nullopt;
    return out; // written == out.size() for this construction
}

} // namespace forumer::crypto