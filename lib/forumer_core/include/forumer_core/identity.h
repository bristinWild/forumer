#pragma once

// Accounts, personas and identity rotation.
//
// An ACCOUNT is one master secret (32 random bytes, kept in the vault on disk).
// It is never published and never signs anything public.
//
// A PERSONA is a public identity derived from the master secret:
//
//   persona_seed[i] = KDF(master, id = i, context = "frmrpers")
//   persona_key[i]  = Ed25519 key pair from persona_seed[i]
//
// Personas are independent: without the master secret nobody can tell that two
// personas belong to the same account. Because they are derived, nothing about
// them needs storing - the recovery phrase brings every one of them back.
//
// The account hands out personas according to its ROTATION POLICY:
//
//   Keep    always the current persona (a stable public identity)
//   Manual  the current persona until the user calls rotate()
//   Auto    a brand-new persona for every post and reply
//
// and the per-post DISCLOSURE choice:
//
//   Persona    signed by the policy's persona; shown as its fingerprint
//   Alias      signed by the policy's persona; shown under a chosen name
//   Anonymous  signed by a fresh one-time persona, whatever the policy
//
// Persona indices are allocated from a counter: indices [0, nextIndex) have
// been handed out. The non-secret bookkeeping (label, policy, counters, alias)
// lives in AccountState (with followed domains), which the app saves next to
// the vault.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "forumer_core/bytes.h"
#include "forumer_core/crypto.h"

namespace forumer::identity {

enum class RotationPolicy { Keep, Manual, Auto };
enum class Disclosure { Persona, Alias, Anonymous };

std::string_view toString(RotationPolicy policy);
std::string_view toString(Disclosure disclosure);
std::optional<RotationPolicy> rotationPolicyFrom(std::string_view text);
std::optional<Disclosure> disclosureFrom(std::string_view text);

/// Short, readable form of a persona public key, e.g. "fr:7Q4K-M2XD".
/// 40 bits of BLAKE2b(public key) in Crockford base32 - for people to tell
/// personas apart at a glance, not a security identifier (the full public key
/// is what signatures are checked against).
std::string fingerprint(const Bytes& publicKey);

/// A persona ready to sign with.
struct Persona {
    uint64_t index = 0;
    crypto::SigningKeyPair keys;

    const Bytes& publicKey() const { return keys.publicKey; }
    std::string fingerprint() const { return identity::fingerprint(keys.publicKey); }
    Bytes sign(const Bytes& message) const { return crypto::sign(keys.secretKey, message); }
};

/// Everything about an account except the master secret. Safe to store in
/// clear (it reveals nothing that links personas), and serialised as JSON.
struct AccountState {
    std::string id;                       // local id, derived from the master secret
    std::string label;                    // user's name for the account, e.g. "Main"
    RotationPolicy policy = RotationPolicy::Manual;
    Disclosure defaultDisclosure = Disclosure::Persona;
    std::string alias;                    // name shown with Disclosure::Alias
    uint64_t currentIndex = 0;            // persona used by Keep / Manual
    uint64_t nextIndex = 1;               // first never-allocated index
    std::vector<std::string> followed;    // followed domains, in the order followed

    /// Most domains one account can follow.
    static constexpr size_t kMaxFollowed = 64;

    std::string toJson() const;
    static std::optional<AccountState> fromJson(std::string_view json);
};

class Account {
public:
    /// A brand-new account with a fresh random master secret.
    static Account create(std::string label);

    /// Rebuild an account from its master secret (after unlocking the vault or
    /// restoring from a recovery phrase) and its saved state. Returns nullopt
    /// if the secret is the wrong size or the state belongs to another account.
    static std::optional<Account> load(SecretBytes master, AccountState state);

    /// A restored account with default settings, its counters positioned past
    /// the highest persona index for which `isKnown(publicKey)` is true (see
    /// recoverNextIndex). Used by "restore from recovery phrase".
    static std::optional<Account> restore(SecretBytes master, std::string label,
                                          const std::function<bool(const Bytes&)>& isKnown);

    const AccountState& state() const { return state_; }
    const SecretBytes& masterSecret() const { return master_; }

    void setLabel(std::string label) { state_.label = std::move(label); }
    void setPolicy(RotationPolicy policy) { state_.policy = policy; }
    void setDefaultDisclosure(Disclosure d) { state_.defaultDisclosure = d; }
    void setAlias(std::string alias) { state_.alias = std::move(alias); }

    /// Follow / unfollow a domain. The caller normalises it first (see
    /// post::normalizeDomains). follow() returns false if the domain is empty
    /// or the list is full; both are no-ops for a domain already in (or not
    /// in) the list.
    bool follow(const std::string& domain);
    void unfollow(const std::string& domain);
    bool isFollowing(const std::string& domain) const;

    /// The persona with a given index (pure derivation; changes no state).
    Persona persona(uint64_t index) const;

    /// The persona Keep/Manual currently post as.
    Persona currentPersona() const { return persona(state_.currentIndex); }

    /// Move to a never-used persona. This is the "Rotate now" button; it also
    /// works under Keep (it just isn't offered in the UI there).
    Persona rotate();

    /// The persona to sign the next post or reply with, applying the rotation
    /// policy and the disclosure. Allocates a new index for Auto and for
    /// Anonymous - the caller must save state() afterwards.
    Persona personaForPost(Disclosure disclosure);
    Persona personaForPost() { return personaForPost(state_.defaultDisclosure); }

private:
    Account(SecretBytes master, AccountState state);
    Persona allocateFresh();

    SecretBytes master_;
    AccountState state_;
};

/// Local account id for a master secret: 16 hex characters, derived (not
/// random) so a restored account gets the same id. Never published.
std::string accountIdFor(const SecretBytes& master);

/// After a restore, find where to resume allocating personas: scan indices
/// 0, 1, 2, … and return one past the highest index whose public key
/// `isKnown`, stopping after `gapLimit` consecutive unknown indices (as HD
/// wallets do). Returns 0 if none are known.
uint64_t recoverNextIndex(const SecretBytes& master,
                          const std::function<bool(const Bytes&)>& isKnown,
                          uint64_t gapLimit = 50);

} // namespace forumer::identity