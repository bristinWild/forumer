#pragma once

// Small encrypted files for an account's private data (its settings, and the
// list of posts it wrote), keyed by Account::storageKey():
//
//   { "v": 1, "nonce": "<b64url>", "ct": "<b64url>" }      XChaCha20-Poly1305
//
// `purpose` (e.g. "state") is bound in as associated data together with the
// account id, so one account's file can't be swapped in for another's, or a
// state file passed off as a post list. Text in, text out: writing the file
// is the caller's job (writeFileAtomic with ownerOnly).

#include <optional>
#include <string>
#include <string_view>

#include "forumer_core/bytes.h"

namespace forumer::sealed {

std::optional<std::string> seal(const SecretBytes& key, std::string_view accountId,
                                std::string_view purpose, std::string_view plaintext);

/// nullopt if the text isn't a sealed file, or the key, account or purpose
/// don't match (wrong account, tampering).
std::optional<std::string> open(const SecretBytes& key, std::string_view accountId,
                                std::string_view purpose, std::string_view text);

} // namespace forumer::sealed