#pragma once

// AccountStore: the accounts on this device, persisted under one directory.
//
//   <dir>/accounts.json   { "v":2, "selected":"<id>", "order":["<id>", ...],
//                           "labels":{"<id>":"Main", ...} }
//   <dir>/<id>/vault.json master secret, encrypted under the password
//   <dir>/<id>/state.enc  AccountState (alias, policy, persona counters, followed
//                         domains), encrypted under the account's storage key
//   <dir>/<id>/<name>.enc other private files of the account (savePrivate)
//
// Only the account's name is readable without unlocking it - the lock screen
// shows it. Everything else that could tie posts to an account is encrypted
// with a key derived from its master secret (see sealed.h). Directories are
// 0700 and files 0600.
//
// Accounts written by Forumer 0.2.2 and earlier kept their state in clear in
// <id>/state.json; the first unlock moves it into state.enc and deletes it.
//
// Each account has its own password. Files are written atomically (to a
// temporary file, then renamed), so a crash mid-write never leaves a torn
// vault behind. The store holds no unlocked secrets itself: unlock() hands
// back an Account, and the caller keeps it for as long as it stays unlocked.

#include <filesystem>
#include <functional>
#include <optional>
#include <utility>
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

    /// An encrypted private file of the account, `<name>.enc` (name is
    /// [a-z]+). load returns "" when the file doesn't exist yet and nullopt
    /// when it exists but can't be opened (damaged, or not this account's).
    std::optional<std::string> loadPrivate(const identity::Account& account,
                                           const std::string& name) const;
    bool savePrivate(const identity::Account& account, const std::string& name,
                     const std::string& plaintext) const;

private:
    struct Index {
        std::string selected;
        std::vector<std::string> order;
        std::vector<std::pair<std::string, std::string>> labels;  // id -> label
        std::string labelOf(const std::string& id) const;
        void setLabel(const std::string& id, const std::string& label);
    };
    Index readIndex() const;
    bool writeIndex(const Index& index) const;
    std::filesystem::path accountDir(const std::string& id) const;
    bool writeAccountFiles(const identity::Account& account, const std::string& vaultJson);
    bool writeState(const identity::Account& account) const;
    /// The account's state: state.enc, or a legacy state.json. `legacy` says
    /// which (so the caller can migrate it).
    std::optional<identity::AccountState> readState(const std::string& id, const SecretBytes& master,
                                                    bool* legacy = nullptr) const;

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