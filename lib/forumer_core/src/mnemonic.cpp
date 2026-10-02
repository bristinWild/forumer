#include "forumer_core/mnemonic.h"

#include <sodium.h>

#include <array>
#include <cctype>
#include <vector>

namespace forumer::mnemonic {

namespace {

const char* const kWords[2048] = {
#include "bip39_english.inc"
};

constexpr size_t kBitsPerWord = 11;
constexpr size_t kTotalBytes = kSecretBytes + 1; // secret + 1 checksum byte (264 bits)

// Read the 11-bit group at `index` from a big-endian bit string.
uint32_t readBits(const uint8_t* bytes, size_t index) {
    uint32_t value = 0;
    for (size_t b = 0; b < kBitsPerWord; ++b) {
        const size_t bit = index * kBitsPerWord + b;
        const uint32_t set = (bytes[bit / 8] >> (7 - bit % 8)) & 1u;
        value = (value << 1) | set;
    }
    return value;
}

// Write `value` as the 11-bit group at `index`.
void writeBits(uint8_t* bytes, size_t index, uint32_t value) {
    for (size_t b = 0; b < kBitsPerWord; ++b) {
        const size_t bit = index * kBitsPerWord + b;
        if ((value >> (kBitsPerWord - 1 - b)) & 1u) bytes[bit / 8] |= uint8_t(1u << (7 - bit % 8));
    }
}

uint8_t checksumByte(const uint8_t* secret) {
    uint8_t hash[crypto_hash_sha256_BYTES];
    crypto_hash_sha256(hash, secret, kSecretBytes);
    const uint8_t first = hash[0];
    sodium_memzero(hash, sizeof hash);
    return first;
}

// Exact match, or a unique prefix of at least 4 letters. -1 if not found.
int lookup(const std::string& word) {
    if (word.empty()) return -1;
    int prefixMatch = -1;
    int prefixCount = 0;
    for (int i = 0; i < 2048; ++i) {
        const std::string_view candidate(kWords[i]);
        if (candidate == word) return i;
        if (word.size() >= 4 && candidate.substr(0, word.size()) == word) {
            prefixMatch = i;
            ++prefixCount;
        }
    }
    return prefixCount == 1 ? prefixMatch : -1;
}

std::vector<std::string> splitWords(std::string_view phrase) {
    std::vector<std::string> words;
    std::string current;
    for (char c : phrase) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!current.empty()) words.push_back(std::move(current)), current.clear();
        } else {
            current.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }
    if (!current.empty()) words.push_back(std::move(current));
    return words;
}

} // namespace

std::string encode(const SecretBytes& secret) {
    if (secret.size() != kSecretBytes) return {};

    std::array<uint8_t, kTotalBytes> bits{};
    std::copy(secret.data(), secret.data() + kSecretBytes, bits.begin());
    bits[kSecretBytes] = checksumByte(secret.data());

    std::string phrase;
    for (size_t i = 0; i < kWordCount; ++i) {
        if (i > 0) phrase.push_back(' ');
        phrase += kWords[readBits(bits.data(), i)];
    }
    sodium_memzero(bits.data(), bits.size());
    return phrase;
}

DecodeResult decode(std::string_view phrase) {
    std::vector<std::string> words = splitWords(phrase);
    auto wipeWords = [&words] { for (auto& w : words) wipe(w); };

    if (words.size() != kWordCount) {
        wipeWords();
        return {std::nullopt, DecodeError::WrongWordCount};
    }

    std::array<uint8_t, kTotalBytes> bits{};
    for (size_t i = 0; i < kWordCount; ++i) {
        const int index = lookup(words[i]);
        if (index < 0) {
            DecodeResult r{std::nullopt, DecodeError::UnknownWord, words[i], i + 1};
            wipeWords();
            sodium_memzero(bits.data(), bits.size());
            return r;
        }
        writeBits(bits.data(), i, static_cast<uint32_t>(index));
    }
    wipeWords();

    SecretBytes secret(bits.data(), kSecretBytes);
    const bool checksumOk = checksumByte(secret.data()) == bits[kSecretBytes];
    sodium_memzero(bits.data(), bits.size());

    if (!checksumOk) return {std::nullopt, DecodeError::BadChecksum};
    return {std::move(secret), DecodeError::None};
}

std::string describe(const DecodeResult& result) {
    switch (result.error) {
        case DecodeError::None: return "ok";
        case DecodeError::WrongWordCount: return "a recovery phrase has exactly 24 words";
        case DecodeError::UnknownWord:
            return "word " + std::to_string(result.badWordIndex) + " (\"" + result.badWord +
                   "\") is not in the recovery word list";
        case DecodeError::BadChecksum:
            return "these words don't form a valid recovery phrase; check for a typo or "
                   "words in the wrong order";
    }
    return "unknown error";
}

void wipe(std::string& phrase) {
    if (!phrase.empty()) sodium_memzero(phrase.data(), phrase.size());
    phrase.clear();
    phrase.shrink_to_fit();
}

} // namespace forumer::mnemonic