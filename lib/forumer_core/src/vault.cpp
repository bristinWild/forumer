#include "forumer_core/vault.h"

#include <nlohmann/json.hpp>

namespace forumer::vault {

using json = nlohmann::json;

namespace {

constexpr const char* kKdfName = "argon2id13";

// Bound into every ciphertext as associated data (see the header comment).
Bytes associatedData(int version) {
    return bytesOf("forumer-vault-v" + std::to_string(version));
}

// Read a base64url string field; nullopt if absent, not a string, or invalid.
std::optional<Bytes> b64Field(const json& doc, const char* key) {
    auto it = doc.find(key);
    if (it == doc.end() || !it->is_string()) return std::nullopt;
    return fromBase64Url(it->get<std::string>());
}

// Read a positive integer field.
std::optional<uint64_t> uintField(const json& doc, const char* key) {
    auto it = doc.find(key);
    if (it == doc.end() || !it->is_number_unsigned()) return std::nullopt;
    const uint64_t v = it->get<uint64_t>();
    if (v == 0) return std::nullopt;
    return v;
}

} // namespace

std::optional<std::string> seal(const SecretBytes& secret, std::string_view password,
                                const crypto::PasswordHashParams& params) {
    if (password.empty()) return std::nullopt;

    const Bytes salt = crypto::randomBytes(crypto::kPwSaltBytes);
    auto key = crypto::deriveKeyFromPassword(password, salt, params);
    if (!key) return std::nullopt;

    auto sealed = crypto::aeadEncrypt(*key, secret.data(), secret.size(),
                                      associatedData(kFormatVersion));
    if (!sealed) return std::nullopt;

    json doc = {
        {"v", kFormatVersion},
        {"kdf", kKdfName},
        {"ops", params.opsLimit},
        {"mem", params.memLimit},
        {"salt", toBase64Url(salt)},
        {"nonce", toBase64Url(sealed->nonce)},
        {"ct", toBase64Url(sealed->ciphertext)},
    };
    return doc.dump();
}

OpenResult open(std::string_view vaultJson, std::string_view password) {
    const json doc = json::parse(vaultJson, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object()) return {std::nullopt, OpenError::Malformed};

    auto version = doc.find("v");
    if (version == doc.end() || !version->is_number_integer())
        return {std::nullopt, OpenError::Malformed};
    if (version->get<int>() != kFormatVersion) return {std::nullopt, OpenError::UnsupportedVersion};

    auto kdf = doc.find("kdf");
    if (kdf == doc.end() || !kdf->is_string() || kdf->get<std::string>() != kKdfName)
        return {std::nullopt, OpenError::UnsupportedVersion};

    const auto ops = uintField(doc, "ops");
    const auto mem = uintField(doc, "mem");
    const auto salt = b64Field(doc, "salt");
    const auto nonce = b64Field(doc, "nonce");
    const auto ct = b64Field(doc, "ct");
    if (!ops || !mem || !salt || !nonce || !ct) return {std::nullopt, OpenError::Malformed};
    if (salt->size() != crypto::kPwSaltBytes || nonce->size() != crypto::kAeadNonceBytes)
        return {std::nullopt, OpenError::Malformed};

    auto key = crypto::deriveKeyFromPassword(password, *salt, {*ops, *mem});
    if (!key) return {std::nullopt, OpenError::KdfFailed};

    auto secret = crypto::aeadDecrypt(*key, {*nonce, *ct}, associatedData(kFormatVersion));
    if (!secret) return {std::nullopt, OpenError::WrongPassword};
    return {std::move(secret), OpenError::None};
}

ResealResult changePassword(std::string_view vaultJson, std::string_view oldPassword,
                            std::string_view newPassword,
                            const crypto::PasswordHashParams& params) {
    OpenResult opened = open(vaultJson, oldPassword);
    if (!opened.ok()) return {std::nullopt, opened.error};

    auto resealed = seal(*opened.secret, newPassword, params);
    if (!resealed) return {std::nullopt, OpenError::KdfFailed};
    return {std::move(resealed), OpenError::None};
}

const char* describe(OpenError error) {
    switch (error) {
        case OpenError::None: return "ok";
        case OpenError::Malformed: return "the identity file is damaged or not a Forumer vault";
        case OpenError::UnsupportedVersion: return "the identity file was created by a newer Forumer";
        case OpenError::WrongPassword: return "wrong password";
        case OpenError::KdfFailed: return "could not derive the key (not enough memory?)";
    }
    return "unknown error";
}

} // namespace forumer::vault