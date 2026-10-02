#include "forumer_core/account_store.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <system_error>

#include "forumer_core/mnemonic.h"
#include "forumer_core/vault.h"

namespace forumer {

namespace fs = std::filesystem;
using json = nlohmann::json;
using identity::Account;
using identity::AccountState;

namespace {

constexpr int kIndexVersion = 1;
constexpr size_t kMinPasswordLength = 8;

// Account ids are 16 lowercase hex characters (identity::accountIdFor). Checked
// before an id is ever used in a path, so nothing like "../x" can escape dir_.
bool isValidId(const std::string& id) {
    if (id.size() != 16) return false;
    return std::all_of(id.begin(), id.end(),
                       [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

std::string passwordProblem(const std::string& password) {
    if (password.size() < kMinPasswordLength)
        return "the password must be at least " + std::to_string(kMinPasswordLength) +
               " characters";
    return {};
}

} // namespace

//  File helpers 

bool writeFileAtomic(const fs::path& path, const std::string& contents, bool ownerOnly) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) return false;

    fs::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        // Restrict before any secret bytes are written.
        if (ownerOnly) {
            fs::permissions(tmp, fs::perms::owner_read | fs::perms::owner_write,
                            fs::perm_options::replace, ec);
            if (ec) return false;
        }
        out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        out.flush();
        if (!out) return false;
    }
    fs::rename(tmp, path, ec);  // atomic replace on POSIX
    if (ec) {
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

std::optional<std::string> readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream buf;
    buf << in.rdbuf();
    return buf.str();
}

//  AccountStore 

AccountStore::AccountStore(fs::path dir, crypto::PasswordHashParams params)
    : dir_(std::move(dir)), params_(params) {}

fs::path AccountStore::accountDir(const std::string& id) const { return dir_ / id; }

AccountStore::Index AccountStore::readIndex() const {
    Index index;
    auto text = readFile(dir_ / "accounts.json");
    if (!text) return index;
    const json doc = json::parse(*text, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object() || doc.value("v", 0) != kIndexVersion) return index;

    index.selected = doc.value("selected", std::string());
    if (auto it = doc.find("order"); it != doc.end() && it->is_array()) {
        for (const auto& id : *it)
            if (id.is_string() && isValidId(id.get<std::string>()))
                index.order.push_back(id.get<std::string>());
    }
    return index;
}

bool AccountStore::writeIndex(const Index& index) const {
    json doc = {{"v", kIndexVersion}, {"selected", index.selected}, {"order", index.order}};
    return writeFileAtomic(dir_ / "accounts.json", doc.dump(2));
}

std::vector<AccountStore::Summary> AccountStore::list() const {
    std::vector<Summary> out;
    for (const auto& id : readIndex().order) {
        auto text = readFile(accountDir(id) / "state.json");
        if (!text) continue;
        auto state = AccountState::fromJson(*text);
        if (!state || state->id != id) continue;
        out.push_back({id, state->label});
    }
    return out;
}

std::optional<std::string> AccountStore::selectedId() const {
    const Index index = readIndex();
    const auto accounts = list();
    for (const auto& a : accounts)
        if (a.id == index.selected) return a.id;
    if (!accounts.empty()) return accounts.front().id;  // fall back to the oldest
    return std::nullopt;
}

bool AccountStore::select(const std::string& id) {
    if (!isValidId(id)) return false;
    Index index = readIndex();
    if (std::find(index.order.begin(), index.order.end(), id) == index.order.end()) return false;
    index.selected = id;
    return writeIndex(index);
}

bool AccountStore::writeAccountFiles(const Account& account, const std::string& vaultJson) {
    const fs::path dir = accountDir(account.state().id);
    if (!writeFileAtomic(dir / "vault.json", vaultJson, /*ownerOnly=*/true)) return false;
    if (!writeFileAtomic(dir / "state.json", account.state().toJson())) return false;

    Index index = readIndex();
    if (std::find(index.order.begin(), index.order.end(), account.state().id) == index.order.end())
        index.order.push_back(account.state().id);
    index.selected = account.state().id;
    return writeIndex(index);
}

AccountStore::Result AccountStore::create(const std::string& label, const std::string& password,
                                          std::string& phrase) {
    if (auto problem = passwordProblem(password); !problem.empty()) return {std::nullopt, problem};

    Account account = Account::create(label.empty() ? "Main" : label);
    auto vaultJson = vault::seal(account.masterSecret(), password, params_);
    if (!vaultJson) return {std::nullopt, "could not encrypt the new identity"};
    if (!writeAccountFiles(account, *vaultJson))
        return {std::nullopt, "could not save the new identity to disk"};

    phrase = mnemonic::encode(account.masterSecret());
    return {std::move(account), {}};
}

AccountStore::Result AccountStore::unlock(const std::string& id, const std::string& password) const {
    if (!isValidId(id)) return {std::nullopt, "unknown account"};
    const fs::path dir = accountDir(id);

    auto vaultText = readFile(dir / "vault.json");
    auto stateText = readFile(dir / "state.json");
    if (!vaultText || !stateText) return {std::nullopt, "this account's files are missing"};

    auto opened = vault::open(*vaultText, password);
    if (!opened.ok()) return {std::nullopt, vault::describe(opened.error)};

    auto state = AccountState::fromJson(*stateText);
    if (!state) return {std::nullopt, "this account's settings file is damaged"};

    auto account = Account::load(std::move(*opened.secret), std::move(*state));
    if (!account) return {std::nullopt, "this account's vault and settings don't match"};
    return {std::move(account), {}};
}

AccountStore::Result AccountStore::restore(const std::string& phrase, const std::string& label,
                                           const std::string& password,
                                           const std::function<bool(const Bytes&)>& isKnown) {
    if (auto problem = passwordProblem(password); !problem.empty()) return {std::nullopt, problem};

    auto decoded = mnemonic::decode(phrase);
    if (!decoded.ok()) return {std::nullopt, mnemonic::describe(decoded)};
    SecretBytes master = std::move(*decoded.secret);

    // Same phrase as an account already on this device: keep its settings.
    const std::string id = identity::accountIdFor(master);
    std::optional<Account> account;
    if (auto stateText = readFile(accountDir(id) / "state.json")) {
        if (auto state = AccountState::fromJson(*stateText))
            account = Account::load(master, std::move(*state));
    }
    if (!account) account = Account::restore(master, label.empty() ? "Restored" : label, isKnown);
    if (!account) return {std::nullopt, "could not rebuild the identity from that phrase"};

    auto vaultJson = vault::seal(account->masterSecret(), password, params_);
    if (!vaultJson) return {std::nullopt, "could not encrypt the restored identity"};
    if (!writeAccountFiles(*account, *vaultJson))
        return {std::nullopt, "could not save the restored identity to disk"};
    return {std::move(account), {}};
}

bool AccountStore::save(const Account& account) {
    const std::string& id = account.state().id;
    if (!isValidId(id)) return false;
    return writeFileAtomic(accountDir(id) / "state.json", account.state().toJson());
}

std::string AccountStore::changePassword(const std::string& id, const std::string& oldPassword,
                                         const std::string& newPassword) {
    if (!isValidId(id)) return "unknown account";
    if (auto problem = passwordProblem(newPassword); !problem.empty()) return problem;

    auto vaultText = readFile(accountDir(id) / "vault.json");
    if (!vaultText) return "this account's files are missing";

    auto result = vault::changePassword(*vaultText, oldPassword, newPassword, params_);
    if (!result.vaultJson) return vault::describe(result.error);
    if (!writeFileAtomic(accountDir(id) / "vault.json", *result.vaultJson, /*ownerOnly=*/true))
        return "could not save the new password";
    return {};
}

bool AccountStore::remove(const std::string& id) {
    if (!isValidId(id)) return false;
    std::error_code ec;
    fs::remove_all(accountDir(id), ec);
    if (ec) return false;

    Index index = readIndex();
    index.order.erase(std::remove(index.order.begin(), index.order.end(), id), index.order.end());
    if (index.selected == id) index.selected = index.order.empty() ? "" : index.order.front();
    return writeIndex(index);
}

} // namespace forumer