#include "forumer_core/forums.h"

#include <nlohmann/json.hpp>

#include "forumer_core/crypto.h"

namespace forumer::forums {

using json = nlohmann::json;

namespace {

constexpr int kVersion = 1;
constexpr std::string_view kWireContext = "frmrpfwr";
constexpr std::string_view kStoreContext = "frmrpfst";
constexpr std::string_view kStoredPrefix = "pf1:";

SecretBytes subkey(const Forum& forum, uint64_t n, std::string_view context) {
    auto key = crypto::deriveSubkey(forum.key, n, context);
    return key ? std::move(*key) : SecretBytes();
}

Bytes ad(std::string_view what, std::string_view id) {
    std::string s = "forumer/pf/v1/";
    s += what;
    s += '/';
    s += id;
    return bytesOf(s);
}

} // namespace

std::string idFor(const SecretBytes& key) {
    const Bytes hash = crypto::blake2b(Bytes(key.data(), key.data() + key.size()), 16);
    return toHex(hash.data(), 8);
}

std::string topicName(std::string_view id) { return "pf-" + std::string(id); }

bool validName(std::string_view name) {
    if (name.empty() || name.size() > kMaxName) return false;
    if (name.front() == ' ' || name.back() == ' ') return false;
    // Valid UTF-8, no control characters.
    for (size_t i = 0; i < name.size();) {
        const auto c = static_cast<uint8_t>(name[i]);
        int len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xe ? 3 : (c >> 3) == 0x1e ? 4 : 0;
        if (len == 0 || i + len > name.size()) return false;
        if (len == 1 && (c < 0x20 || c == 0x7f)) return false;
        for (int k = 1; k < len; ++k)
            if ((static_cast<uint8_t>(name[i + k]) & 0xc0) != 0x80) return false;
        i += len;
    }
    return true;
}

std::optional<Forum> fromKey(const SecretBytes& key, std::string_view name) {
    if (key.size() != kKeyBytes || !validName(name)) return std::nullopt;
    Forum f;
    f.key = key;
    f.id = idFor(key);
    f.name = std::string(name);
    return f;
}

std::optional<Forum> create(std::string_view name) {
    return fromKey(crypto::randomSecret(kKeyBytes), name);
}

std::string keyToText(const SecretBytes& key) { return toBase64Url(key.data(), key.size()); }

std::optional<SecretBytes> keyFromText(std::string_view text) {
    auto bytes = fromBase64Url(text);
    if (!bytes || bytes->size() != kKeyBytes) return std::nullopt;
    SecretBytes key(bytes->data(), bytes->size());
    std::fill(bytes->begin(), bytes->end(), 0);
    return key;
}

std::vector<uint8_t> sealWire(const Forum& forum, const std::vector<uint8_t>& payload) {
    auto sealed = crypto::aeadEncrypt(subkey(forum, 1, kWireContext), payload.data(), payload.size(),
                                      ad("wire", forum.id));
    if (!sealed) return {};
    const json doc = {
        {"v", kVersion},
        {"t", "sealed"},
        {"f", forum.id},
        {"n", toBase64Url(sealed->nonce)},
        {"ct", toBase64Url(sealed->ciphertext)},
    };
    const std::string text = doc.dump();
    return std::vector<uint8_t>(text.begin(), text.end());
}

std::optional<std::vector<uint8_t>> openWire(const Forum& forum, const std::vector<uint8_t>& sealed) {
    if (sealed.empty() || sealed.size() > kMaxSealedPayload) return std::nullopt;
    const json doc = json::parse(sealed.begin(), sealed.end(), nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object() || doc.value("v", 0) != kVersion ||
        doc.value("t", std::string()) != "sealed" || doc.value("f", std::string()) != forum.id)
        return std::nullopt;
    auto nonce = fromBase64Url(doc.value("n", std::string()));
    auto ct = fromBase64Url(doc.value("ct", std::string()));
    if (!nonce || !ct) return std::nullopt;
    auto plain = crypto::aeadDecrypt(subkey(forum, 1, kWireContext),
                                     crypto::Sealed{std::move(*nonce), std::move(*ct)},
                                     ad("wire", forum.id));
    if (!plain) return std::nullopt;
    return std::vector<uint8_t>(plain->data(), plain->data() + plain->size());
}

std::string sealStored(const Forum& forum, std::string_view envelopeJson) {
    auto sealed = crypto::aeadEncrypt(subkey(forum, 2, kStoreContext),
                                      reinterpret_cast<const uint8_t*>(envelopeJson.data()),
                                      envelopeJson.size(), ad("store", forum.id));
    if (!sealed) return {};
    Bytes all = sealed->nonce;
    all.insert(all.end(), sealed->ciphertext.begin(), sealed->ciphertext.end());
    return std::string(kStoredPrefix) + toBase64Url(all);
}

std::optional<std::string> openStored(const Forum& forum, std::string_view stored) {
    if (!isSealedStored(stored)) return std::nullopt;
    auto all = fromBase64Url(stored.substr(kStoredPrefix.size()));
    if (!all || all->size() < crypto::kAeadNonceBytes + crypto::kAeadTagBytes) return std::nullopt;
    crypto::Sealed sealed;
    sealed.nonce.assign(all->begin(), all->begin() + crypto::kAeadNonceBytes);
    sealed.ciphertext.assign(all->begin() + crypto::kAeadNonceBytes, all->end());
    auto plain = crypto::aeadDecrypt(subkey(forum, 2, kStoreContext), sealed, ad("store", forum.id));
    if (!plain) return std::nullopt;
    return std::string(reinterpret_cast<const char*>(plain->data()), plain->size());
}

bool isSealedStored(std::string_view stored) {
    return stored.substr(0, kStoredPrefix.size()) == kStoredPrefix;
}

} // namespace forumer::forums