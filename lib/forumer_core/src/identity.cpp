#include "forumer_core/identity.h"

#include <algorithm>

#include <nlohmann/json.hpp>

#include <utility>

namespace forumer::identity {

using json = nlohmann::json;

namespace {

// crypto_kdf contexts are exactly 8 characters and name the key's purpose.
constexpr std::string_view kPersonaContext = "frmrpers";
constexpr std::string_view kAccountIdContext = "frmracct";
constexpr std::string_view kAnonymousContext = "frmranon";
constexpr std::string_view kStorageContext = "frmrstor";

constexpr int kStateVersion = 1;

} // namespace

//  Enum <-> text 

std::string_view toString(RotationPolicy policy) {
    switch (policy) {
        case RotationPolicy::Keep: return "keep";
        case RotationPolicy::Manual: return "manual";
        case RotationPolicy::Auto: return "auto";
    }
    return "manual";
}

std::string_view toString(Disclosure disclosure) {
    switch (disclosure) {
        case Disclosure::Persona: return "persona";
        case Disclosure::Alias: return "alias";
        case Disclosure::Anonymous: return "anonymous";
    }
    return "persona";
}

std::optional<RotationPolicy> rotationPolicyFrom(std::string_view text) {
    if (text == "keep") return RotationPolicy::Keep;
    if (text == "manual") return RotationPolicy::Manual;
    if (text == "auto") return RotationPolicy::Auto;
    return std::nullopt;
}

std::optional<Disclosure> disclosureFrom(std::string_view text) {
    if (text == "persona") return Disclosure::Persona;
    if (text == "alias") return Disclosure::Alias;
    if (text == "anonymous") return Disclosure::Anonymous;
    return std::nullopt;
}

//  Fingerprint 

std::string fingerprint(const Bytes& publicKey) {
    // Crockford base32: no I, L, O, U, so it reads back unambiguously.
    static constexpr char kAlphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    const Bytes hash = crypto::blake2b(publicKey, 16);

    // First 40 bits -> 8 characters of 5 bits each.
    uint64_t bits = 0;
    for (int i = 0; i < 5; ++i) bits = (bits << 8) | hash[i];

    std::string out = "fr:";
    for (int i = 7; i >= 0; --i) {
        out.push_back(kAlphabet[(bits >> (i * 5)) & 0x1f]);
        if (i == 4) out.push_back('-');
    }
    return out;
}

//  AccountState 

std::string AccountState::toJson() const {
    json doc = {
        {"v", kStateVersion},
        {"id", id},
        {"label", label},
        {"policy", std::string(toString(policy))},
        {"disclosure", std::string(toString(defaultDisclosure))},
        {"alias", alias},
        {"current", currentIndex},
        {"next", nextIndex},
        {"followed", followed},
    };
    if (restored) doc["restored"] = true;
    return doc.dump();
}

std::optional<AccountState> AccountState::fromJson(std::string_view text) {
    const json doc = json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object()) return std::nullopt;
    if (doc.value("v", 0) != kStateVersion) return std::nullopt;

    try {
        AccountState s;
        s.id = doc.at("id").get<std::string>();
        s.label = doc.value("label", std::string());
        s.alias = doc.value("alias", std::string());
        s.currentIndex = doc.at("current").get<uint64_t>();
        s.nextIndex = doc.at("next").get<uint64_t>();

        // Added after v1 shipped: optional, and tolerant of junk entries.
        if (auto f = doc.find("followed"); f != doc.end() && f->is_array()) {
            for (const auto& d : *f) {
                if (!d.is_string() || d.get<std::string>().empty()) continue;
                if (s.followed.size() == AccountState::kMaxFollowed) break;
                const std::string name = d.get<std::string>();
                if (std::find(s.followed.begin(), s.followed.end(), name) == s.followed.end())
                    s.followed.push_back(name);
            }
        }

        s.restored = doc.value("restored", false);

        auto policy = rotationPolicyFrom(doc.value("policy", std::string("manual")));
        auto disclosure = disclosureFrom(doc.value("disclosure", std::string("persona")));
        if (!policy || !disclosure) return std::nullopt;
        s.policy = *policy;
        s.defaultDisclosure = *disclosure;

        // Invariant: the current persona has been allocated.
        if (s.id.empty() || s.nextIndex <= s.currentIndex) return std::nullopt;
        return s;
    } catch (const json::exception&) {
        return std::nullopt;
    }
}

//  Followed domains 

bool Account::follow(const std::string& domain) {
    if (domain.empty()) return false;
    if (isFollowing(domain)) return true;
    if (state_.followed.size() >= AccountState::kMaxFollowed) return false;
    state_.followed.push_back(domain);
    return true;
}

void Account::unfollow(const std::string& domain) {
    auto& f = state_.followed;
    f.erase(std::remove(f.begin(), f.end(), domain), f.end());
}

bool Account::isFollowing(const std::string& domain) const {
    const auto& f = state_.followed;
    return std::find(f.begin(), f.end(), domain) != f.end();
}

//  Free functions 

std::string accountIdFor(const SecretBytes& master) {
    auto key = crypto::deriveSubkey(master, 0, kAccountIdContext);
    if (!key) return {};
    const Bytes hash = crypto::blake2b(Bytes(key->data(), key->data() + key->size()), 16);
    return toHex(hash.data(), 8);
}

namespace {

std::optional<crypto::SigningKeyPair> derivePersonaKeys(const SecretBytes& master, uint64_t index) {
    auto seed = crypto::deriveSubkey(master, index, kPersonaContext, crypto::kSeedBytes);
    if (!seed) return std::nullopt;
    return crypto::signingKeyPairFromSeed(*seed);
}

} // namespace

uint64_t recoverNextIndex(const SecretBytes& master,
                          const std::function<bool(const Bytes&)>& isKnown,
                          uint64_t gapLimit) {
    uint64_t next = 0;
    uint64_t misses = 0;
    for (uint64_t i = 0; misses < gapLimit; ++i) {
        auto keys = derivePersonaKeys(master, i);
        if (!keys) break;
        if (isKnown(keys->publicKey)) {
            next = i + 1;
            misses = 0;
        } else {
            ++misses;
        }
    }
    return next;
}

std::vector<Bytes> personaKeys(const SecretBytes& master, uint64_t from, uint64_t to) {
    std::vector<Bytes> out;
    for (uint64_t i = from; i < to; ++i) {
        auto keys = derivePersonaKeys(master, i);
        if (!keys) break;
        out.push_back(std::move(keys->publicKey));
    }
    return out;
}

//  Account 

Account::Account(SecretBytes master, AccountState state)
    : master_(std::move(master)), state_(std::move(state)) {}

Account Account::create(std::string label) {
    SecretBytes master = crypto::randomSecret(crypto::kSeedBytes);
    AccountState state;
    state.id = accountIdFor(master);
    state.label = std::move(label);
    return Account(std::move(master), std::move(state));
}

std::optional<Account> Account::load(SecretBytes master, AccountState state) {
    if (master.size() != crypto::kSeedBytes) return std::nullopt;
    if (state.id != accountIdFor(master)) return std::nullopt;  // wrong vault for this state
    if (state.nextIndex <= state.currentIndex) return std::nullopt;
    return Account(std::move(master), std::move(state));
}

std::optional<Account> Account::restore(SecretBytes master, std::string label,
                                        const std::function<bool(const Bytes&)>& isKnown) {
    if (master.size() != crypto::kSeedBytes) return std::nullopt;
    AccountState state;
    state.id = accountIdFor(master);
    state.label = std::move(label);

    const uint64_t next = recoverNextIndex(master, isKnown);
    if (next > 0) {
        // Resume as the most recently used persona; new ones start after it.
        state.currentIndex = next - 1;
        state.nextIndex = next;
    }
    // This device may not hold the account's whole history yet, so newer
    // personas may still turn up: see notePersonaUsed() and restorePending().
    state.restored = true;
    return Account(std::move(master), std::move(state));
}

bool Account::notePersonaUsed(uint64_t index, bool resumeAs) {
    bool changed = false;
    if (index + 1 > state_.nextIndex) {
        state_.nextIndex = index + 1;
        changed = true;
    }
    // Resume as the newest persona this account is known to have used - the
    // one the user had rotated to - not an older one it rotated away from.
    if (resumeAs && index > state_.currentIndex) {
        state_.currentIndex = index;
        changed = true;
    }
    return changed;
}

bool Account::skipKnown(const std::function<bool(const Bytes&)>& isKnown) {
    bool changed = false;
    for (int guard = 0; guard < 10'000; ++guard) {
        auto keys = derivePersonaKeys(master_, state_.nextIndex);
        if (!keys || !isKnown(keys->publicKey)) break;
        ++state_.nextIndex;
        changed = true;
    }
    return changed;
}

SecretBytes storageKeyFor(const SecretBytes& master) {
    auto key = crypto::deriveSubkey(master, 0, kStorageContext);
    return key ? std::move(*key) : SecretBytes();
}

SecretBytes Account::storageKey() const { return storageKeyFor(master_); }

Persona Account::oneTime() const {
    // A random 64-bit id under its own context: never reused, never recovered.
    uint64_t id = 0;
    const Bytes r = crypto::randomBytes(sizeof id);
    for (uint8_t b : r) id = (id << 8) | b;
    auto seed = crypto::deriveSubkey(master_, id, kAnonymousContext, crypto::kSeedBytes);
    auto keys = seed ? crypto::signingKeyPairFromSeed(*seed) : std::nullopt;
    return Persona{id, keys ? std::move(*keys) : crypto::SigningKeyPair{}};
}

Persona Account::persona(uint64_t index) const {
    auto keys = derivePersonaKeys(master_, index);
    // Cannot fail for a valid 32-byte master (checked on construction).
    return Persona{index, keys ? std::move(*keys) : crypto::SigningKeyPair{}};
}

Persona Account::allocateFresh() {
    return persona(state_.nextIndex++);
}

Persona Account::rotate() {
    Persona fresh = allocateFresh();
    state_.currentIndex = fresh.index;
    return fresh;
}

Persona Account::personaForPost(Disclosure disclosure) {
    // Anonymous posts never reuse an identity and never move the current one.
    if (disclosure == Disclosure::Anonymous) return oneTime();

    switch (state_.policy) {
        case RotationPolicy::Keep:
        case RotationPolicy::Manual:
            return currentPersona();
        case RotationPolicy::Auto:
            // Each post gets its own persona; it also becomes "current", so the
            // profile shows the latest one.
            return rotate();
    }
    return currentPersona();
}

} // namespace forumer::identity