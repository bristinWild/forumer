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
//   Anonymous  signed by a one-time key, whatever the policy
//
// Anonymous keys are derived under their own context ("frmranon") from a
// random 64-bit id, never from the persona counter: a restore on a fresh
// device can't hand one out again, and nothing about them can be recovered
// or linked later (they don't show up in "My posts" after a restore).
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

/// Everything about an account except the master secret, serialised as JSON.
/// Stored encrypted (see AccountStore): the alias and persona counters say
/// more about an account than anyone holding the disk should learn.
struct AccountState {
    std::string id;                       // local id, derived from the master secret
    std::string label;                    // user's name for the account, e.g. "Main"
    RotationPolicy policy = RotationPolicy::Manual;
    Disclosure defaultDisclosure = Disclosure::Persona;
    std::string alias;                    // name shown with Disclosure::Alias
    uint64_t currentIndex = 0;            // persona used by Keep / Manual
    uint64_t nextIndex = 1;               // first never-allocated index
    std::vector<std::string> followed;    // followed domains, in the order followed
    bool restored = false;                // rebuilt from a phrase; personas still being recovered
    int64_t restoredAtMs = 0;             // when (while `restored`): replies older than this were seen elsewhere

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

    /// Restore bookkeeping. While restored() is true the account was rebuilt
    /// from its phrase and not every persona it used may be known yet; the
    /// app holds posting until history has arrived (or the user says go).
    bool restorePending() const { return state_.restored; }
    int64_t restoredAtMs() const { return state_.restoredAtMs; }
    void finishRestore() {
        state_.restored = false;
        state_.restoredAtMs = 0;
    }

    /// A post signed by persona `index` of this account turned up (from
    /// history, or from another device with the same phrase): never allocate
    /// it or anything below it again, and - with `resumeAs`, while a restore
    /// is pending - post as the newest one seen, not an older one the user had
    /// rotated away from. Returns true if the state changed.
    bool notePersonaUsed(uint64_t index, bool resumeAs);

    /// Move nextIndex past any persona `isKnown` reports as already used,
    /// so a post never reuses one. Returns true if the state changed.
    bool skipKnown(const std::function<bool(const Bytes&)>& isKnown);

    /// A key for encrypting this account's private files (state, own posts),
    /// derived from the master secret.
    SecretBytes storageKey() const;

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
    Persona oneTime() const;

    SecretBytes master_;
    AccountState state_;
};

/// Local account id for a master secret: 16 hex characters, derived (not
/// random) so a restored account gets the same id. Never published.
std::string accountIdFor(const SecretBytes& master);

/// The key Account::storageKey() returns, from the master secret alone (the
/// account store needs it before it has the account's state).
SecretBytes storageKeyFor(const SecretBytes& master);

/// After a restore, find where to resume allocating personas: scan indices
/// 0, 1, 2, … and return one past the highest index whose public key
/// `isKnown`, stopping after `gapLimit` consecutive unknown indices (as HD
/// wallets do). Returns 0 if none are known. The default gap is generous:
/// pressing "rotate" without posting leaves unused indices behind.
inline constexpr uint64_t kRecoveryGap = 1000;
uint64_t recoverNextIndex(const SecretBytes& master,
                          const std::function<bool(const Bytes&)>& isKnown,
                          uint64_t gapLimit = kRecoveryGap);

/// Public keys of personas [from, to) of the account with this master secret,
/// for spotting this account's posts as history arrives after a restore.
std::vector<Bytes> personaKeys(const SecretBytes& master, uint64_t from, uint64_t to);

} // namespace forumer::identity