#pragma once

// Signed post envelopes — the unit of content in Forumer.
//
// Every post and reply is an immutable, self-authenticating document:
//
//   canonical = JSON of every field except `nonce`, `sig` and `id`,
//               keys sorted, no whitespace
//   work      = find `nonce` so BLAKE2b(canonical || nonce) has >= powBits
//               leading zero bits                                (anti-spam)
//   sig       = Ed25519(persona, canonical || nonce)             (authenticity)
//   id        = hex(BLAKE2b-128(canonical || nonce || sig))      (content address)
//
// So anyone can check, without trusting the sender or any server, that a post
// was written by the persona whose key it carries, hasn't been altered, paid
// its proof-of-work, and has the id it claims. Retrying a send re-sends the
// identical bytes, so duplicates are harmless.
//
// The persona key is always present (it is what the signature is checked
// against); the DISCLOSURE only controls how the author is shown.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "forumer_core/bytes.h"
#include "forumer_core/identity.h"

namespace forumer::post {

inline constexpr int kFormatVersion = 1;

// Size limits (characters of UTF-8, counted as bytes). Kept modest: media
// travels by reference (CID), never inside a post.
inline constexpr size_t kMaxTitle = 200;
inline constexpr size_t kMaxBody = 10000;
inline constexpr size_t kMaxDomains = 3;
inline constexpr size_t kMaxDomainLength = 32;
inline constexpr size_t kMaxAlias = 32;
inline constexpr size_t kMaxMedia = 4;
inline constexpr size_t kMaxEncoded = 16 * 1024;  // whole wire envelope

enum class Kind { Post, Reply };

/// A file attached to a post, stored in Logos Storage and referenced by CID.
struct MediaRef {
    std::string cid;
    std::string mime;
    uint64_t size = 0;
    std::string sha256;  // hex, lets the viewer check what it downloaded
};

/// What the author writes. sign() turns it into a Post.
struct Draft {
    Kind kind = Kind::Post;
    std::string forum = "public";   // or a private forum id
    std::string root;               // replies: id of the post at the top of the thread
    std::string parent;             // replies: id of the post/reply being answered
    std::string title;              // posts only
    std::string body;
    std::vector<std::string> domains;  // posts only: 1..kMaxDomains, see normalizeDomains()
    std::vector<MediaRef> media;
    int64_t timestampMs = 0;           // author's clock, ms since epoch
};

struct Post {
    int version = kFormatVersion;
    Draft content;
    Bytes publicKey;                                   // persona that signed
    identity::Disclosure disclosure = identity::Disclosure::Persona;
    std::string alias;                                 // Alias disclosure only
    bool showFingerprint = true;
    int powBits = 0;
    Bytes nonce;
    Bytes signature;
    std::string id;                                    // 32 hex chars

    /// How the author appears to readers: "Anonymous", "night owl · fr:7Q4K-M2XD",
    /// "night owl" (alias without fingerprint) or "fr:7Q4K-M2XD".
    std::string authorDisplay() const;

    /// The wire / storage form (JSON, includes nonce, sig and id).
    std::string toJson() const;
    static std::optional<Post> fromJson(std::string_view json);
};

enum class Error {
    None,
    Malformed,         // missing/invalid fields, bad encodings, wrong version
    TooLarge,          // breaks a size limit
    InsufficientWork,  // proof-of-work below what this kind of post requires
    BadSignature,      // not signed by the key it carries, or altered after signing
    BadId,             // id doesn't match the content
};

const char* describe(Error error);

/// Trim, lowercase, collapse inner whitespace to '-', keep [a-z0-9-], drop
/// tags outside 2..kMaxDomainLength characters, remove duplicates (first wins),
/// cap at kMaxDomains. "Privacy, privacy , Campus Life" -> {"privacy","campus-life"}.
std::vector<std::string> normalizeDomains(const std::vector<std::string>& raw);

/// Split comma-separated user input, then normalizeDomains().
std::vector<std::string> parseDomains(std::string_view commaSeparated);

/// Check a draft is well-formed before signing (shape and size limits).
Error validateDraft(const Draft& draft);

/// Sign a draft as `persona`. Mines proof-of-work of `powBits` first (cost
/// roughly doubles per bit). Returns nullopt with `error` set if the draft is
/// invalid. Domains are normalised as part of signing.
std::optional<Post> sign(Draft draft, const identity::Persona& persona,
                         identity::Disclosure disclosure, const std::string& alias, int powBits,
                         Error* error = nullptr);

/// Full check of a received post: shape, sizes, proof-of-work (at least
/// `minPowBits`), signature and id. Anything not Error::None must be dropped.
Error verify(const Post& post, int minPowBits);

/// Leading zero bits of BLAKE2b(canonical || nonce) — exposed for tests and
/// for benchmarking PoW cost on a device.
int workBits(const Post& post);

} // namespace forumer::post