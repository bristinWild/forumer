#pragma once

// Images attached to posts, stored in Logos Storage.
//
// A post carries a MediaRef per image (post.h): {cid, mime, size, sha256},
// all inside what the author signs. The file in Storage is not the image but
// a sealed copy of it, so the storage network - nodes, the DHT, anyone who
// stumbles on a CID - holds only ciphertext. The key comes from the image
// itself (convergent encryption):
//
//   h      = SHA-256(image)                      (the signed `sha256`)
//   key    = BLAKE2b-256("forumer/media/v1/key" || h)
//            keyed with KDF(forumKey, 3, "frmrpfmd") in a private forum
//   nonce  = BLAKE2b-192("forumer/media/v1/nonce" || key)
//   blob   = "FRM1" || XChaCha20-Poly1305(key, nonce, image, ad = "forumer/media/v1/" || mime)
//
// So whoever can read the post can open its images, and nobody else can.
// The same image always seals to the same blob (in the same forum), so
// re-attaching it reuses the stored copy. In the public forum, someone who
// already has an image can tell whether a given blob is that image; in a
// private forum the forum key is mixed in, so outsiders can't even do that.
//
// After opening, the image is checked against the signed hash and size, and
// its first bytes against the declared type: a swapped or altered file is
// refused, never shown.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "forumer_core/bytes.h"
#include "forumer_core/post.h"

namespace forumer::media {

inline constexpr size_t kMaxImageBytes = 5 * 1024 * 1024;   // per image, after re-encoding
inline constexpr size_t kMaxPerPost = post::kMaxMedia;      // 4
inline constexpr size_t kBlobOverhead = 4 + 16;             // magic + Poly1305 tag

/// The image types a post may carry (what the backend re-encodes to).
bool allowedMime(std::string_view mime);

/// "image/jpeg" / "image/png" from the first bytes, or "" for anything else.
std::string sniff(const uint8_t* data, size_t size);
inline std::string sniff(const Bytes& data) { return sniff(data.data(), data.size()); }

/// SHA-256 as 64 lowercase hex characters.
std::string sha256Hex(const uint8_t* data, size_t size);

/// Whether `cid` looks like a Storage CID (base58 / base32 text, 20..128 chars).
bool cidShape(std::string_view cid);

/// Whether a reference from a post is one this version can fetch and show:
/// an allowed type, a size within the limit, a hash, a CID.
bool usable(const post::MediaRef& ref);

struct Sealed {
    Bytes blob;           // what goes into Storage
    std::string sha256;   // of the image - goes into the MediaRef
    uint64_t size = 0;    // of the image
};

/// Seal an image for upload. `forumKey` is the private forum's key, or null
/// for the public forum. nullopt if the type isn't allowed, the bytes aren't
/// that type, or it is too large.
std::optional<Sealed> seal(const Bytes& image, std::string_view mime,
                           const SecretBytes* forumKey = nullptr);

/// Open a blob fetched for `ref` and check it. nullopt unless it is exactly
/// the image the author signed.
std::optional<Bytes> open(const Bytes& blob, const post::MediaRef& ref,
                          const SecretBytes* forumKey = nullptr);

// ── Previews ─────────────────────────────────────────────────────────────────
//
// A small JPEG of each image travels over Delivery right after its post
// (sync.h "mprev"), so a thread shows its pictures as soon as it opens,
// without anyone dialling a storage node. It is sealed like the image, with
// a key derived from the full image's (signed) hash, and signed by the
// post's own key together with the post id and index: nobody but the author
// can attach a preview to a post.

inline constexpr size_t kMaxPreviewBytes = 96 * 1024;   // the JPEG, before sealing

/// Seal a preview JPEG of the image `full` refers to.
std::optional<Bytes> sealPreview(const Bytes& jpeg, const post::MediaRef& full,
                                 const SecretBytes* forumKey = nullptr);
/// Open one; nullopt unless it is a JPEG sealed for `full`.
std::optional<Bytes> openPreview(const Bytes& blob, const post::MediaRef& full,
                                 const SecretBytes* forumKey = nullptr);
/// What the author signs for preview `index` of post `postId`.
Bytes previewMessage(const std::string& postId, size_t index, const Bytes& blob);

} // namespace forumer::media
