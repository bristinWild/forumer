#include "forumer_core/media.h"

#include <sodium.h>

#include <cstring>

#include "forumer_core/crypto.h"

namespace forumer::media {

namespace {

constexpr uint8_t kMagic[4] = {'F', 'R', 'M', '1'};
constexpr std::string_view kKeyLabel = "forumer/media/v1/key";
constexpr std::string_view kNonceLabel = "forumer/media/v1/nonce";
constexpr std::string_view kAdLabel = "forumer/media/v1/";
constexpr std::string_view kForumContext = "frmrpfmd";

bool isHex64(std::string_view s) {
    if (s.size() != 64) return false;
    for (char c : s)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

Bytes hexBytes(std::string_view hex) {
    Bytes out(hex.size() / 2);
    for (size_t i = 0; i < out.size(); ++i) {
        auto nib = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
        out[i] = static_cast<uint8_t>(nib(hex[2 * i]) << 4 | nib(hex[2 * i + 1]));
    }
    return out;
}

// The key and nonce for an image with hash `h` (32 bytes).
struct KeyNonce {
    uint8_t key[crypto_aead_xchacha20poly1305_ietf_KEYBYTES];
    uint8_t nonce[crypto_aead_xchacha20poly1305_ietf_NPUBBYTES];
    ~KeyNonce() { sodium_memzero(key, sizeof key); }
};

bool derive(const Bytes& h, const SecretBytes* forumKey, KeyNonce& out,
            std::string_view label = kKeyLabel) {
    Bytes msg(label.begin(), label.end());
    msg.insert(msg.end(), h.begin(), h.end());
    if (forumKey) {
        auto mk = crypto::deriveSubkey(*forumKey, 3, kForumContext);
        if (!mk) return false;
        crypto_generichash(out.key, sizeof out.key, msg.data(), msg.size(), mk->data(), mk->size());
    } else {
        crypto_generichash(out.key, sizeof out.key, msg.data(), msg.size(), nullptr, 0);
    }
    Bytes nmsg(kNonceLabel.begin(), kNonceLabel.end());
    nmsg.insert(nmsg.end(), out.key, out.key + sizeof out.key);
    crypto_generichash(out.nonce, sizeof out.nonce, nmsg.data(), nmsg.size(), nullptr, 0);
    return true;
}

Bytes adFor(std::string_view mime) {
    Bytes ad(kAdLabel.begin(), kAdLabel.end());
    ad.insert(ad.end(), mime.begin(), mime.end());
    return ad;
}

} // namespace

bool allowedMime(std::string_view mime) { return mime == "image/jpeg" || mime == "image/png"; }

std::string sniff(const uint8_t* d, size_t n) {
    static const uint8_t png[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    if (n >= 8 && std::memcmp(d, png, 8) == 0) return "image/png";
    if (n >= 3 && d[0] == 0xff && d[1] == 0xd8 && d[2] == 0xff) return "image/jpeg";
    return {};
}

std::string sha256Hex(const uint8_t* data, size_t size) {
    uint8_t h[crypto_hash_sha256_BYTES];
    crypto_hash_sha256(h, data, size);
    return toHex(h, sizeof h);
}

bool cidShape(std::string_view cid) {
    if (cid.size() < 20 || cid.size() > 128) return false;
    for (char c : cid)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) return false;
    return true;
}

bool usable(const post::MediaRef& ref) {
    return allowedMime(ref.mime) && ref.size > 0 && ref.size <= kMaxImageBytes && isHex64(ref.sha256) &&
           cidShape(ref.cid);
}

std::optional<Sealed> seal(const Bytes& image, std::string_view mime, const SecretBytes* forumKey) {
    if (!allowedMime(mime) || image.empty() || image.size() > kMaxImageBytes || sniff(image) != mime)
        return std::nullopt;
    Sealed out;
    out.size = image.size();
    out.sha256 = sha256Hex(image.data(), image.size());
    KeyNonce kn;
    if (!derive(hexBytes(out.sha256), forumKey, kn)) return std::nullopt;
    const Bytes ad = adFor(mime);
    out.blob.resize(sizeof kMagic + image.size() + crypto_aead_xchacha20poly1305_ietf_ABYTES);
    std::memcpy(out.blob.data(), kMagic, sizeof kMagic);
    unsigned long long written = 0;
    crypto_aead_xchacha20poly1305_ietf_encrypt(out.blob.data() + sizeof kMagic, &written, image.data(),
                                               image.size(), ad.data(), ad.size(), nullptr, kn.nonce,
                                               kn.key);
    out.blob.resize(sizeof kMagic + written);
    return out;
}

std::optional<Bytes> open(const Bytes& blob, const post::MediaRef& ref, const SecretBytes* forumKey) {
    if (!usable(ref)) return std::nullopt;
    if (blob.size() != ref.size + kBlobOverhead || std::memcmp(blob.data(), kMagic, sizeof kMagic) != 0)
        return std::nullopt;
    KeyNonce kn;
    if (!derive(hexBytes(ref.sha256), forumKey, kn)) return std::nullopt;
    const Bytes ad = adFor(ref.mime);
    Bytes image(ref.size);
    unsigned long long got = 0;
    if (crypto_aead_xchacha20poly1305_ietf_decrypt(image.data(), &got, nullptr, blob.data() + sizeof kMagic,
                                                   blob.size() - sizeof kMagic, ad.data(), ad.size(),
                                                   kn.nonce, kn.key) != 0 ||
        got != ref.size)
        return std::nullopt;
    if (sha256Hex(image.data(), image.size()) != ref.sha256 || sniff(image) != ref.mime) return std::nullopt;
    return image;
}

namespace {
constexpr std::string_view kPreviewKeyLabel = "forumer/media/v1/preview-key";
constexpr uint8_t kPreviewMagic[4] = {'F', 'R', 'P', '1'};
} // namespace

std::optional<Bytes> sealPreview(const Bytes& jpeg, const post::MediaRef& full, const SecretBytes* forumKey) {
    if (jpeg.empty() || jpeg.size() > kMaxPreviewBytes || sniff(jpeg) != "image/jpeg" || !isHex64(full.sha256))
        return std::nullopt;
    KeyNonce kn;
    if (!derive(hexBytes(full.sha256), forumKey, kn, kPreviewKeyLabel)) return std::nullopt;
    const Bytes ad = adFor("image/jpeg");
    Bytes blob(sizeof kPreviewMagic + jpeg.size() + crypto_aead_xchacha20poly1305_ietf_ABYTES);
    std::memcpy(blob.data(), kPreviewMagic, sizeof kPreviewMagic);
    unsigned long long written = 0;
    crypto_aead_xchacha20poly1305_ietf_encrypt(blob.data() + sizeof kPreviewMagic, &written, jpeg.data(),
                                               jpeg.size(), ad.data(), ad.size(), nullptr, kn.nonce, kn.key);
    blob.resize(sizeof kPreviewMagic + written);
    return blob;
}

std::optional<Bytes> openPreview(const Bytes& blob, const post::MediaRef& full, const SecretBytes* forumKey) {
    if (blob.size() <= kBlobOverhead || blob.size() > kMaxPreviewBytes + kBlobOverhead ||
        std::memcmp(blob.data(), kPreviewMagic, sizeof kPreviewMagic) != 0 || !isHex64(full.sha256))
        return std::nullopt;
    KeyNonce kn;
    if (!derive(hexBytes(full.sha256), forumKey, kn, kPreviewKeyLabel)) return std::nullopt;
    const Bytes ad = adFor("image/jpeg");
    Bytes jpeg(blob.size() - kBlobOverhead);
    unsigned long long got = 0;
    if (crypto_aead_xchacha20poly1305_ietf_decrypt(jpeg.data(), &got, nullptr, blob.data() + sizeof kPreviewMagic,
                                                   blob.size() - sizeof kPreviewMagic, ad.data(), ad.size(),
                                                   kn.nonce, kn.key) != 0)
        return std::nullopt;
    jpeg.resize(got);
    if (sniff(jpeg) != "image/jpeg") return std::nullopt;
    return jpeg;
}

Bytes previewMessage(const std::string& postId, size_t index, const Bytes& blob) {
    std::string s = "forumer/preview/v1|" + postId + "|" + std::to_string(index) + "|" +
                    sha256Hex(blob.data(), blob.size());
    return Bytes(s.begin(), s.end());
}

} // namespace forumer::media