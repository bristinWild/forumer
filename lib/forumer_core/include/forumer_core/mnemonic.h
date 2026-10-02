#pragma once

// Recovery phrase: the 32-byte master secret as 24 English words (BIP-39).
//
// 32 bytes = 256 bits. BIP-39 appends an 8-bit checksum (the first byte of
// SHA-256 of the secret) and splits the 264 bits into 24 groups of 11 bits;
// each group picks one word from a fixed list of 2048. The checksum means a
// single mistyped or swapped word is caught when restoring instead of
// silently producing a different identity.
//
// Only the encoding is BIP-39. Forumer uses the 32 bytes directly as its
// master secret — there is no BIP-39 passphrase/seed stretching and no wallet
// derivation, so a Forumer phrase is not a crypto-wallet phrase.

#include <optional>
#include <string>
#include <string_view>

#include "forumer_core/bytes.h"

namespace forumer::mnemonic {

inline constexpr size_t kSecretBytes = 32;
inline constexpr size_t kWordCount = 24;

/// The phrase for a 32-byte secret: 24 lowercase words separated by single
/// spaces. Returns an empty string if `secret` is not 32 bytes.
///
/// The returned string IS the secret in another form: show it to the user,
/// never log or store it, and clear it when done (see wipe()).
std::string encode(const SecretBytes& secret);

enum class DecodeError {
    None,
    WrongWordCount,  // not exactly 24 words
    UnknownWord,     // a word not in the list (see DecodeResult::badWord)
    BadChecksum,     // all words valid, but they don't form a valid phrase
};

struct DecodeResult {
    std::optional<SecretBytes> secret;  // set on success
    DecodeError error = DecodeError::None;
    std::string badWord;                // for UnknownWord: what the user typed
    size_t badWordIndex = 0;            // for UnknownWord: 1-based position

    bool ok() const { return secret.has_value(); }
};

/// Parse a phrase back into the secret. Forgiving about input: case, extra
/// spaces, tabs and newlines are ignored, and any word may be shortened to its
/// first 4+ letters as long as that prefix is unambiguous (BIP-39 words are
/// unique in their first four letters).
DecodeResult decode(std::string_view phrase);

/// Human-readable message for an error (for UI).
std::string describe(const DecodeResult& result);

/// Overwrite a string that held a phrase, then clear it.
void wipe(std::string& phrase);

} // namespace forumer::mnemonic