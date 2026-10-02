#include "forumer_core/bytes.h"

#include <sodium.h>

#include <utility>

namespace forumer {

// SecretBytes

SecretBytes::SecretBytes(size_t size) : buf_(size, 0) {}

SecretBytes::SecretBytes(const uint8_t* data, size_t size) : buf_(data, data + size) {}

SecretBytes::SecretBytes(const SecretBytes& other) : buf_(other.buf_) {}

SecretBytes::SecretBytes(SecretBytes&& other) noexcept : buf_(std::move(other.buf_)) {
    other.buf_.clear();
}

SecretBytes& SecretBytes::operator=(const SecretBytes& other) {
    if (this != &other) {
        wipe();
        buf_ = other.buf_;
    }
    return *this;
}

SecretBytes& SecretBytes::operator=(SecretBytes&& other) noexcept {
    if (this != &other) {
        wipe();
        buf_ = std::move(other.buf_);
        other.buf_.clear();
    }
    return *this;
}

SecretBytes::~SecretBytes() { wipe(); }

void SecretBytes::wipe() {
    if (!buf_.empty()) sodium_memzero(buf_.data(), buf_.size());
}

void SecretBytes::clear() {
    wipe();
    buf_.clear();
    buf_.shrink_to_fit();
}

bool SecretBytes::equals(const SecretBytes& other) const {
    if (buf_.size() != other.buf_.size()) return false;
    if (buf_.empty()) return true;
    return sodium_memcmp(buf_.data(), other.buf_.data(), buf_.size()) == 0;
}

// --- Hex ---------------------------------------------------------------------

std::string toHex(const uint8_t* data, size_t size) {
    static const char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(size * 2);
    for (size_t i = 0; i < size; ++i) {
        out.push_back(digits[data[i] >> 4]);
        out.push_back(digits[data[i] & 0x0f]);
    }
    return out;
}

std::string toHex(const Bytes& bytes) { return toHex(bytes.data(), bytes.size()); }

namespace {
int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
} // namespace

std::optional<Bytes> fromHex(std::string_view hex) {
    if (hex.size() % 2 != 0) return std::nullopt;
    Bytes out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        const int hi = hexValue(hex[i]);
        const int lo = hexValue(hex[i + 1]);
        if (hi < 0 || lo < 0) return std::nullopt;
        out.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    return out;
}

// --- Base64url (no padding) --------------------------------------------------

std::string toBase64Url(const uint8_t* data, size_t size) {
    const int variant = sodium_base64_VARIANT_URLSAFE_NO_PADDING;
    std::string out(sodium_base64_encoded_len(size, variant), '\0');
    sodium_bin2base64(out.data(), out.size(), data, size, variant);
    out.resize(out.size() - 1); // drop the trailing NUL libsodium writes
    return out;
}

std::string toBase64Url(const Bytes& bytes) { return toBase64Url(bytes.data(), bytes.size()); }

std::optional<Bytes> fromBase64Url(std::string_view text) {
    Bytes out(text.size()); // decoded output is always shorter than the input
    size_t outLen = 0;
    const char* end = nullptr;
    const int rc = sodium_base642bin(out.data(), out.size(), text.data(), text.size(),
                                     nullptr, &outLen, &end,
                                     sodium_base64_VARIANT_URLSAFE_NO_PADDING);
    if (rc != 0 || end != text.data() + text.size()) return std::nullopt;
    out.resize(outLen);
    return out;
}

// --- UTF-8 -------------------------------------------------------------------

Bytes bytesOf(std::string_view text) { return Bytes(text.begin(), text.end()); }

std::string stringOf(const Bytes& bytes) { return std::string(bytes.begin(), bytes.end()); }

} // namespace forumer