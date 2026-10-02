#include "test.h"

#include <regex>
#include <set>

#include "forumer_core/crypto.h"
#include "forumer_core/identity.h"
#include "forumer_core/mnemonic.h"

using namespace forumer;
using namespace forumer::identity;

TEST(identity_personas_are_deterministic_and_distinct) {
    Account a = Account::create("Main");
    CHECK(a.persona(0).publicKey() == a.persona(0).publicKey());
    CHECK(a.persona(0).publicKey() != a.persona(1).publicKey());
    CHECK_EQ(a.persona(7).index, uint64_t(7));

    Account other = Account::create("Other");
    CHECK(a.persona(0).publicKey() != other.persona(0).publicKey());
}

TEST(identity_persona_signatures_verify) {
    Account a = Account::create("Main");
    Persona p = a.persona(3);
    const Bytes msg = bytesOf("a post");
    CHECK(crypto::verify(p.publicKey(), msg, p.sign(msg)));
    CHECK(!crypto::verify(a.persona(4).publicKey(), msg, p.sign(msg)));
}

TEST(identity_fingerprint_format) {
    Account a = Account::create("Main");
    const std::string fp = a.persona(0).fingerprint();
    CHECK(std::regex_match(fp, std::regex("fr:[0-9A-HJKMNP-TV-Z]{4}-[0-9A-HJKMNP-TV-Z]{4}")));
    CHECK_EQ(fp, a.persona(0).fingerprint());
    CHECK(fp != a.persona(1).fingerprint());
}

TEST(identity_keep_and_manual_reuse_the_current_persona) {
    for (auto policy : {RotationPolicy::Keep, RotationPolicy::Manual}) {
        Account a = Account::create("Main");
        a.setPolicy(policy);
        const Bytes first = a.personaForPost(Disclosure::Persona).publicKey();
        const Bytes second = a.personaForPost(Disclosure::Alias).publicKey();
        CHECK(first == second);
        CHECK_EQ(a.state().nextIndex, uint64_t(1));  // nothing new allocated
    }
}

TEST(identity_manual_rotate_switches_persona) {
    Account a = Account::create("Main");
    a.setPolicy(RotationPolicy::Manual);
    const Bytes before = a.personaForPost().publicKey();
    Persona rotated = a.rotate();
    const Bytes after = a.personaForPost().publicKey();
    CHECK(before != after);
    CHECK(after == rotated.publicKey());
    CHECK_EQ(a.state().currentIndex, uint64_t(1));
}

TEST(identity_auto_uses_a_new_persona_every_post) {
    Account a = Account::create("Main");
    a.setPolicy(RotationPolicy::Auto);
    std::set<Bytes> keys;
    for (int i = 0; i < 5; ++i) keys.insert(a.personaForPost(Disclosure::Persona).publicKey());
    CHECK_EQ(keys.size(), size_t(5));
    CHECK_EQ(a.state().nextIndex, uint64_t(6));
}

TEST(identity_anonymous_is_always_fresh_and_keeps_current) {
    Account a = Account::create("Main");
    a.setPolicy(RotationPolicy::Keep);
    const Bytes current = a.currentPersona().publicKey();
    const Bytes anon1 = a.personaForPost(Disclosure::Anonymous).publicKey();
    const Bytes anon2 = a.personaForPost(Disclosure::Anonymous).publicKey();
    CHECK(anon1 != current);
    CHECK(anon2 != anon1);
    CHECK(a.currentPersona().publicKey() == current);  // Keep identity untouched
}

TEST(identity_state_json_round_trip) {
    Account a = Account::create("Campus");
    a.setPolicy(RotationPolicy::Auto);
    a.setDefaultDisclosure(Disclosure::Alias);
    a.setAlias("night owl");
    a.personaForPost();
    a.personaForPost();

    auto restored = AccountState::fromJson(a.state().toJson());
    CHECK(restored.has_value());
    CHECK_EQ(restored->id, a.state().id);
    CHECK_EQ(restored->label, std::string("Campus"));
    CHECK(restored->policy == RotationPolicy::Auto);
    CHECK(restored->defaultDisclosure == Disclosure::Alias);
    CHECK_EQ(restored->alias, std::string("night owl"));
    CHECK_EQ(restored->currentIndex, a.state().currentIndex);
    CHECK_EQ(restored->nextIndex, a.state().nextIndex);

    CHECK(!AccountState::fromJson("{}").has_value());
    CHECK(!AccountState::fromJson("garbage").has_value());
}

TEST(identity_load_checks_the_state_matches_the_secret) {
    Account a = Account::create("Main");
    a.rotate();
    auto reloaded = Account::load(a.masterSecret(), a.state());
    CHECK(reloaded.has_value());
    CHECK(reloaded->currentPersona().publicKey() == a.currentPersona().publicKey());

    Account other = Account::create("Other");
    CHECK(!Account::load(other.masterSecret(), a.state()).has_value());  // wrong vault
}

TEST(identity_account_id_is_derived_from_the_secret) {
    Account a = Account::create("Main");
    CHECK_EQ(a.state().id.size(), size_t(16));
    CHECK_EQ(a.state().id, accountIdFor(a.masterSecret()));
    CHECK(a.state().id != Account::create("Main").state().id);
}

TEST(identity_restore_from_phrase_recovers_personas_and_counter) {
    // Original device: post with personas 0..6, with a gap (3 and 4 unused publicly).
    Account original = Account::create("Main");
    original.setPolicy(RotationPolicy::Auto);
    std::set<Bytes> published;
    for (int i = 0; i < 7; ++i) {
        Persona p = original.personaForPost();
        if (p.index != 3 && p.index != 4) published.insert(p.publicKey());
    }
    const std::string phrase = mnemonic::encode(original.masterSecret());

    // New device: phrase + the messages it caught up on.
    auto decoded = mnemonic::decode(phrase);
    CHECK(decoded.ok());
    auto restored = Account::restore(std::move(*decoded.secret), "Restored",
                                     [&](const Bytes& pk) { return published.count(pk) > 0; });
    CHECK(restored.has_value());
    CHECK_EQ(restored->state().id, original.state().id);
    CHECK_EQ(restored->state().nextIndex, original.state().nextIndex);
    CHECK(restored->persona(6).publicKey() == original.persona(6).publicKey());

    // The next new persona must not collide with any already used.
    restored->setPolicy(RotationPolicy::Auto);
    CHECK(published.count(restored->personaForPost().publicKey()) == 0);
}

TEST(identity_recover_next_index_with_nothing_known) {
    Account a = Account::create("Main");
    CHECK_EQ(recoverNextIndex(a.masterSecret(), [](const Bytes&) { return false; }, 10),
             uint64_t(0));
}