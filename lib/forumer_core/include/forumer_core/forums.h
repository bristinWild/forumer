#pragma once

// Private forums: a forum whose posts only people holding its key can read.
//
// A private forum IS its key: 32 random bytes, made by whoever creates it and
// handed to others in an invite link. Everything else follows from the key:
//
//   id     = hex(BLAKE2b-128(key))[0..16]     (names the forum locally and in posts)
//   topic  = /forumer/<v>/pf-<id>/proto       (its own content topic)
//   wire   key = KDF(key, 1, "frmrpfwr")      (seals every message on the topic)
//   store  key = KDF(key, 2, "frmrpfst")      (seals its posts in the local log)
//
// On the network every message on the forum's topic - posts, digests - is
// sealed (XChaCha20-Poly1305, the forum id bound in as associated data), so
// relays, store nodes and anyone without the link see only ciphertext on an
// opaque topic. Inside, a post is an ordinary signed post (post.h) whose
// `forum` field is the forum id: personas, aliases, anonymous posting,
// proof-of-work and flood limits all work unchanged.
//
// Membership is the link: anyone who has it can read and post. There is no
// admin and no way to remove someone - a forum whose link leaked can only
// be replaced by a new one (create, share the new link).

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "forumer_core/bytes.h"

namespace forumer::forums {

inline constexpr size_t kKeyBytes = 32;
inline constexpr size_t kMaxName = 64;           // bytes of UTF-8
inline constexpr size_t kMaxJoined = 32;         // per account
inline constexpr size_t kMaxSealedPayload = 96 * 1024;   // fits a sealed image-preview piece

struct Forum {
    std::string id;     // 16 lowercase hex
    SecretBytes key;    // kKeyBytes
    std::string name;   // shown in the app; chosen by the creator, travels in the link
};

/// A new forum with a fresh random key. nullopt if the name is unusable
/// (empty after trimming, too long, or not valid UTF-8 without controls).
std::optional<Forum> create(std::string_view name);

/// Rebuild a forum from its key (joining from an invite).
std::optional<Forum> fromKey(const SecretBytes& key, std::string_view name);

/// The id derived from a key, and the forum name used for its content topic
/// (sync::contentTopic(topicName(id))).
std::string idFor(const SecretBytes& key);
std::string topicName(std::string_view id);

/// Whether `name` can be a forum name (see create()).
bool validName(std::string_view name);

/// The key as text for an invite (base64url, 43 characters), and back.
std::string keyToText(const SecretBytes& key);
std::optional<SecretBytes> keyFromText(std::string_view text);

// Network messages on the forum's topic

/// Seal one wire payload (sync::encodePost / encodeDigest output).
std::vector<uint8_t> sealWire(const Forum& forum, const std::vector<uint8_t>& payload);
/// Open one; nullopt if it isn't this forum's, was altered, or is too large.
std::optional<std::vector<uint8_t>> openWire(const Forum& forum, const std::vector<uint8_t>& sealed);

// Posts at rest in the local log (PostStore keeps the `envelope` column
// sealed for private forums, so the log file never holds their text)

std::string sealStored(const Forum& forum, std::string_view envelopeJson);
std::optional<std::string> openStored(const Forum& forum, std::string_view stored);
/// Whether an envelope column value is a sealed one (vs plain post JSON).
bool isSealedStored(std::string_view stored);

} // namespace forumer::forums
