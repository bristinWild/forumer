#include "forumer_core/sealed.h"

#include <nlohmann/json.hpp>

#include "forumer_core/crypto.h"

namespace forumer::sealed {

using json = nlohmann::json;

namespace {

constexpr int kVersion = 1;

Bytes associatedData(std::string_view accountId, std::string_view purpose) {
    std::string ad = "forumer/sealed/v1/";
    ad += purpose;
    ad += '/';
    ad += accountId;
    return bytesOf(ad);
}

} // namespace

std::optional<std::string> seal(const SecretBytes& key, std::string_view accountId,
                                std::string_view purpose, std::string_view plaintext) {
    if (key.size() != crypto::kAeadKeyBytes) return std::nullopt;
    auto sealedData = crypto::aeadEncrypt(key, reinterpret_cast<const uint8_t*>(plaintext.data()),
                                          plaintext.size(), associatedData(accountId, purpose));
    if (!sealedData) return std::nullopt;
    const json doc = {
        {"v", kVersion},
        {"nonce", toBase64Url(sealedData->nonce)},
        {"ct", toBase64Url(sealedData->ciphertext)},
    };
    return doc.dump();
}

std::optional<std::string> open(const SecretBytes& key, std::string_view accountId,
                                std::string_view purpose, std::string_view text) {
    if (key.size() != crypto::kAeadKeyBytes) return std::nullopt;
    const json doc = json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object() || doc.value("v", 0) != kVersion) return std::nullopt;
    auto nonce = fromBase64Url(doc.value("nonce", std::string()));
    auto ct = fromBase64Url(doc.value("ct", std::string()));
    if (!nonce || !ct) return std::nullopt;
    auto plain = crypto::aeadDecrypt(key, crypto::Sealed{std::move(*nonce), std::move(*ct)},
                                     associatedData(accountId, purpose));
    if (!plain) return std::nullopt;
    return std::string(reinterpret_cast<const char*>(plain->data()), plain->size());
}

} // namespace forumer::sealed