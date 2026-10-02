#include "test.h"

#include <chrono>

#include "forumer_core/identity.h"
#include "forumer_core/post.h"

using namespace forumer;
using namespace forumer::post;
using identity::Disclosure;

namespace {

constexpr int kTestPow = 8;  // cheap for tests; the app uses more

int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

Draft topic(std::string title = "Is SIM-ID linking a privacy risk?") {
    Draft d;
    d.kind = Kind::Post;
    d.title = std::move(title);
    d.body = "Asking for a friend.";
    d.domains = {"privacy", "politics"};
    d.timestampMs = nowMs();
    return d;
}

Draft replyTo(const Post& parent) {
    Draft d;
    d.kind = Kind::Reply;
    d.root = parent.content.kind == Kind::Post ? parent.id : parent.content.root;
    d.parent = parent.id;
    d.body = "Yes, metadata alone is enough.";
    d.timestampMs = nowMs();
    return d;
}

} // namespace

TEST(post_sign_and_verify) {
    auto account = identity::Account::create("Main");
    auto p = sign(topic(), account.persona(0), Disclosure::Persona, "", kTestPow);
    CHECK(p.has_value());
    CHECK_EQ(p->id.size(), size_t(32));
    CHECK(workBits(*p) >= kTestPow);
    CHECK(verify(*p, kTestPow) == Error::None);
}

TEST(post_wire_round_trip_still_verifies) {
    auto account = identity::Account::create("Main");
    auto p = sign(topic(), account.persona(0), Disclosure::Alias, "night owl", kTestPow);
    auto back = Post::fromJson(p->toJson());
    CHECK(back.has_value());
    CHECK_EQ(back->id, p->id);
    CHECK_EQ(back->content.title, p->content.title);
    CHECK(verify(*back, kTestPow) == Error::None);
}

TEST(post_tampering_is_detected) {
    auto account = identity::Account::create("Main");
    auto p = sign(topic(), account.persona(0), Disclosure::Persona, "", kTestPow);

    Post body = *p;
    body.content.body = "Edited after signing";
    CHECK(verify(body, kTestPow) != Error::None);

    Post domains = *p;
    domains.content.domains = {"politics"};
    CHECK(verify(domains, kTestPow) != Error::None);

    Post id = *p;
    id.id = std::string(32, 'a');
    CHECK(verify(id, kTestPow) == Error::BadId);
}

TEST(post_cannot_be_reattributed_to_another_persona) {
    auto account = identity::Account::create("Main");
    auto p = sign(topic(), account.persona(0), Disclosure::Persona, "", kTestPow);
    Post stolen = *p;
    stolen.publicKey = account.persona(1).publicKey();
    CHECK(verify(stolen, 0) != Error::None);
}

TEST(post_proof_of_work_is_enforced) {
    auto account = identity::Account::create("Main");
    auto cheap = sign(topic(), account.persona(0), Disclosure::Persona, "", 0);
    CHECK(verify(*cheap, 0) == Error::None);
    CHECK(verify(*cheap, kTestPow) == Error::InsufficientWork);  // receiver demands more

    // Claiming more work than the nonce delivers is caught too.
    Post liar = *cheap;
    liar.powBits = 30;
    CHECK(verify(liar, 0) != Error::None);
}

TEST(post_reply_threading_fields) {
    auto account = identity::Account::create("Main");
    auto root = sign(topic(), account.persona(0), Disclosure::Persona, "", kTestPow);
    auto comment = sign(replyTo(*root), account.persona(1), Disclosure::Persona, "", kTestPow);
    auto nested = sign(replyTo(*comment), account.persona(2), Disclosure::Anonymous, "", kTestPow);
    CHECK(comment.has_value() && nested.has_value());
    CHECK_EQ(comment->content.root, root->id);
    CHECK_EQ(nested->content.root, root->id);       // same thread
    CHECK_EQ(nested->content.parent, comment->id);  // answers the comment
    CHECK(verify(*nested, kTestPow) == Error::None);
}

TEST(post_disclosure_display) {
    auto account = identity::Account::create("Main");
    auto persona = account.persona(0);
    auto asPersona = sign(topic(), persona, Disclosure::Persona, "", kTestPow);
    auto asAlias = sign(topic(), persona, Disclosure::Alias, "night owl", kTestPow);
    auto asAnon = sign(topic(), persona, Disclosure::Anonymous, "ignored", kTestPow);

    CHECK_EQ(asPersona->authorDisplay(), persona.fingerprint());
    CHECK_EQ(asAlias->authorDisplay(), "night owl · " + persona.fingerprint());
    CHECK_EQ(asAnon->authorDisplay(), std::string("Anonymous"));
    CHECK(asAnon->alias.empty());  // alias never leaks into an anonymous post

    // Alias disclosure without an alias is refused.
    Error err = Error::None;
    CHECK(!sign(topic(), persona, Disclosure::Alias, "", kTestPow, &err).has_value());
    CHECK(err == Error::Malformed);
}

TEST(post_draft_validation) {
    auto persona = identity::Account::create("Main").persona(0);
    Error err = Error::None;

    Draft noTitle = topic("");
    CHECK(!sign(noTitle, persona, Disclosure::Persona, "", 0, &err) && err == Error::Malformed);

    Draft noDomains = topic();
    noDomains.domains = {"!!"};
    CHECK(!sign(noDomains, persona, Disclosure::Persona, "", 0, &err) && err == Error::Malformed);

    Draft huge = topic();
    huge.body = std::string(kMaxBody + 1, 'x');
    CHECK(!sign(huge, persona, Disclosure::Persona, "", 0, &err) && err == Error::TooLarge);

    Draft orphanReply;
    orphanReply.kind = Kind::Reply;
    orphanReply.body = "hi";
    orphanReply.timestampMs = nowMs();
    CHECK(!sign(orphanReply, persona, Disclosure::Persona, "", 0, &err) && err == Error::Malformed);

    Draft badUtf8 = topic();
    badUtf8.body = std::string("\xff\xfe broken");
    CHECK(!sign(badUtf8, persona, Disclosure::Persona, "", 0, &err) && err == Error::Malformed);
}

TEST(post_domain_normalisation) {
    CHECK(parseDomains("Privacy, privacy , Campus Life") ==
          std::vector<std::string>({"privacy", "campus-life"}));
    CHECK(parseDomains(" ZK_Proofs ,  , x, a-very-long-domain-name-that-goes-past-the-limit") ==
          std::vector<std::string>({"zk-proofs"}));
    CHECK(parseDomains("a1, b2, c3, d4").size() == kMaxDomains);  // capped at 3
    CHECK(parseDomains("café, tech!") == std::vector<std::string>({"caf", "tech"}));
}

TEST(post_rejects_garbage_json) {
    CHECK(!Post::fromJson("not json").has_value());
    CHECK(!Post::fromJson("{}").has_value());
    CHECK(!Post::fromJson(std::string(kMaxEncoded + 1, ' ')).has_value());
}

TEST(post_ids_are_unique_per_signing) {
    auto persona = identity::Account::create("Main").persona(0);
    Draft d = topic();
    auto a = sign(d, persona, Disclosure::Persona, "", kTestPow);
    auto b = sign(d, persona, Disclosure::Persona, "", kTestPow);
    CHECK(a->id != b->id);  // random nonce start -> distinct ids for identical drafts
}