#pragma once

// AccountStore: the accounts on this device, persisted under one directory.
//
//   <dir>/accounts.json         { "v":1, "selected":"<id>", "order":["<id>", ...] }
//   <dir>/<id>/vault.json       master secret, encrypted (owner read/write only)
//   <dir>/<id>/state.json       AccountState: label, policy, counters (not secret)
//
// Each account has its own password. Files are written atomically (to a
// temporary file, then renamed), so a crash mid-write never leaves a torn
// vault behind. The store holds no unlocked secrets itself: unlock() hands
// back an Account, and the caller keeps it for as long as it stays unlocked.

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "forumer_core/crypto.h"
#include "forumer_core/identity.h"

namespace forumer {

class AccountStore {
public:
    explicit AccountStore(std::filesystem::path dir,
                          crypto::PasswordHashParams params = crypto::defaultPasswordHashParams());

    struct Summary {
        std::string id;
        std::string label;
    };

    /// Accounts in creation order. Accounts whose files are unreadable are skipped.
    std::vector<Summary> list() const;
    bool empty() const { return list().empty(); }

    /// The account to offer for unlock at startup (last one selected), if any.
    std::optional<std::string> selectedId() const;
    bool select(const std::string& id);

    struct Result {
        std::optional<identity::Account> account;  // set on success
        std::string error;                         // human-readable, empty on success
        bool ok() const { return account.has_value(); }
    };

    /// New account with a fresh master secret, sealed under `password` and
    /// selected. On success `phrase` receives the 24-word recovery phrase -
    /// show it once, then mnemonic::wipe() it.
    Result create(const std::string& label, const std::string& password, std::string& phrase);

    /// Open an account's vault with its password.
    Result unlock(const std::string& id, const std::string& password) const;

    /// Rebuild an account from its recovery phrase and seal it under a new
    /// password. If the account already exists on this device its saved state
    /// (label, settings, persona counters) is kept; otherwise the persona
    /// counter is recovered by scanning for known public keys (`isKnown`).
    /// The restored account is selected.
    Result restore(const std::string& phrase, const std::string& label,
                   const std::string& password,
                   const std::function<bool(const Bytes&)>& isKnown);

    /// Persist an account's state (call after anything that changes it -
    /// settings, rotation, or a post that allocated a new persona).
    bool save(const identity::Account& account);

    /// Re-seal an account's vault under a new password. Empty string on success.
    std::string changePassword(const std::string& id, const std::string& oldPassword,
                               const std::string& newPassword);

    /// Delete an account's files from this device (irreversible without its
    /// recovery phrase). Selection moves to another account if needed.
    bool remove(const std::string& id);

private:
    struct Index {
        std::string selected;
        std::vector<std::string> order;
    };
    Index readIndex() const;
    bool writeIndex(const Index& index) const;
    std::filesystem::path accountDir(const std::string& id) const;
    bool writeAccountFiles(const identity::Account& account, const std::string& vaultJson);

    std::filesystem::path dir_;
    crypto::PasswordHashParams params_;
};

/// Write `contents` to `path` atomically: write a sibling temp file, flush,
/// then rename over the target. If `ownerOnly`, the file is created with
/// permissions 0600. Creates parent directories as needed.
bool writeFileAtomic(const std::filesystem::path& path, const std::string& contents,
                     bool ownerOnly = false);

/// Whole file as a string; nullopt if it can't be read.
std::optional<std::string> readFile(const std::filesystem::path& path);

} // namespace forumer