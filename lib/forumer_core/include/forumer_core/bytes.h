#pragma once

// Byte buffers and text encodings used across forumer_core.
//
// forumer_core is deliberately free of Qt and the Logos SDK so it can be unit
// tested on its own and reused by other modules. Everything here is plain
// C++20 on top of libsodium.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace forumer {

/// Ordinary (non-secret) bytes: public keys, signatures, ciphertexts, hashes.
using Bytes = std::vector<uint8_t>;

/// Bytes that hold secret material (master secret, private keys, derived
/// encryption keys). Memory is wiped with sodium_memzero when the buffer is
/// destroyed or cleared, so secrets don't linger on the heap.
///
/// Copying is allowed (persona derivation needs it) but every copy is wiped
/// independently. Never log or serialise one except through the vault.
class SecretBytes {
public:
    SecretBytes() = default;
    explicit SecretBytes(size_t size);
    SecretBytes(const uint8_t* data, size_t size);
    SecretBytes(const SecretBytes& other);
    SecretBytes(SecretBytes&& other) noexcept;
    SecretBytes& operator=(const SecretBytes& other);
    SecretBytes& operator=(SecretBytes&& other) noexcept;
    ~SecretBytes();

    uint8_t* data() { return buf_.data(); }
    const uint8_t* data() const { return buf_.data(); }
    size_t size() const { return buf_.size(); }
    bool empty() const { return buf_.empty(); }

    /// Wipe and release the contents.
    void clear();

    /// Constant-time equality (sodium_memcmp); false if sizes differ.
    bool equals(const SecretBytes& other) const;

private:
    void wipe();
    std::vector<uint8_t> buf_;
};

// Encodings

/// Lowercase hex, no prefix.
std::string toHex(const uint8_t* data, size_t size);
std::string toHex(const Bytes& bytes);

/// Accepts upper or lower case; returns nullopt on odd length or bad digits.
std::optional<Bytes> fromHex(std::string_view hex);

/// URL-safe base64 without padding (RFC 4648 §5) - the encoding used for
/// keys, signatures and nonces in Forumer envelopes.
std::string toBase64Url(const uint8_t* data, size_t size);
std::string toBase64Url(const Bytes& bytes);

/// Returns nullopt on invalid input. Padding is not accepted.
std::optional<Bytes> fromBase64Url(std::string_view text);

/// Bytes of a UTF-8 string, and back.
Bytes bytesOf(std::string_view text);
std::string stringOf(const Bytes& bytes);

} // namespace forumer