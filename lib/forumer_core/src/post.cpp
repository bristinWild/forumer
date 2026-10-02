#include "forumer_core/post.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>

#include "forumer_core/crypto.h"

namespace forumer::post {

using json = nlohmann::json;
using identity::Disclosure;

namespace {

constexpr size_t kNonceBytes = 8;
constexpr size_t kIdHexChars = 32;  // BLAKE2b-128

std::string_view kindName(Kind kind) { return kind == Kind::Post ? "post" : "reply"; }

std::optional<Kind> kindFrom(std::string_view s) {
    if (s == "post") return Kind::Post;
    if (s == "reply") return Kind::Reply;
    return std::nullopt;
}

json mediaJson(const std::vector<MediaRef>& media) {
    json arr = json::array();
    for (const auto& m : media)
        arr.push_back({{"cid", m.cid}, {"mime", m.mime}, {"size", m.size}, {"sha256", m.sha256}});
    return arr;
}

// Every signed field. nlohmann::json objects keep keys sorted, and dump()
// without an indent emits no whitespace, so this is deterministic.
json canonicalDoc(const Post& p) {
    const Draft& c = p.content;
    return json{
        {"v", p.version},
        {"kind", std::string(kindName(c.kind))},
        {"forum", c.forum},
        {"root", c.root},
        {"parent", c.parent},
        {"title", c.title},
        {"body", c.body},
        {"domains", c.domains},
        {"media", mediaJson(c.media)},
        {"ts", c.timestampMs},
        {"pk", toBase64Url(p.publicKey)},
        {"disc", std::string(identity::toString(p.disclosure))},
        {"alias", p.alias},
        {"fp", p.showFingerprint},
        {"pow", p.powBits},
    };
}

// Throws json::type_error on invalid UTF-8; callers treat that as Malformed.
Bytes canonicalBytes(const Post& p) { return bytesOf(canonicalDoc(p).dump()); }

Bytes concat(const Bytes& a, const Bytes& b) {
    Bytes out(a);
    out.insert(out.end(), b.begin(), b.end());
    return out;
}

int leadingZeroBits(const Bytes& hash) {
    int bits = 0;
    for (uint8_t byte : hash) {
        if (byte == 0) { bits += 8; continue; }
        for (int i = 7; i >= 0 && !((byte >> i) & 1); --i) ++bits;
        break;
    }
    return bits;
}

std::string computeId(const Bytes& canonical, const Bytes& nonce, const Bytes& sig) {
    return toHex(crypto::blake2b(concat(concat(canonical, nonce), sig), 16));
}

bool isLowerHex(const std::string& s) {
    return std::all_of(s.begin(), s.end(),
                       [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

Error validateDisclosure(const Post& p) {
    switch (p.disclosure) {
        case Disclosure::Anonymous:
            if (!p.alias.empty() || p.showFingerprint) return Error::Malformed;
            break;
        case Disclosure::Alias:
            if (p.alias.empty()) return Error::Malformed;
            if (p.alias.size() > kMaxAlias) return Error::TooLarge;
            break;
        case Disclosure::Persona:
            if (!p.alias.empty() || !p.showFingerprint) return Error::Malformed;
            break;
    }
    return Error::None;
}

} // namespace

// --- Domains -----------------------------------------------------------------

std::vector<std::string> normalizeDomains(const std::vector<std::string>& raw) {
    std::vector<std::string> out;
    for (const auto& tag : raw) {
        std::string clean;
        bool pendingDash = false;
        for (char ch : tag) {
            const unsigned char c = static_cast<unsigned char>(ch);
            if (std::isspace(c) || ch == '-' || ch == '_') {
                pendingDash = !clean.empty();
            } else if (std::isalnum(c) && c < 128) {
                if (pendingDash) clean.push_back('-');
                pendingDash = false;
                clean.push_back(static_cast<char>(std::tolower(c)));
            }
            // anything else (punctuation, non-ASCII) is dropped
        }
        if (clean.size() < 2 || clean.size() > kMaxDomainLength) continue;
        if (std::find(out.begin(), out.end(), clean) != out.end()) continue;
        out.push_back(clean);
        if (out.size() == kMaxDomains) break;
    }
    return out;
}

std::vector<std::string> parseDomains(std::string_view commaSeparated) {
    std::vector<std::string> parts;
    std::string current;
    for (char c : commaSeparated) {
        if (c == ',') { parts.push_back(current); current.clear(); }
        else current.push_back(c);
    }
    parts.push_back(current);
    return normalizeDomains(parts);
}

// --- Validation --------------------------------------------------------------

Error validateDraft(const Draft& d) {
    if (d.forum.empty() || d.forum.size() > 64) return Error::Malformed;
    if (d.timestampMs <= 0) return Error::Malformed;
    if (d.body.size() > kMaxBody || d.title.size() > kMaxTitle) return Error::TooLarge;
    if (d.media.size() > kMaxMedia) return Error::TooLarge;
    for (const auto& m : d.media)
        if (m.cid.empty() || m.cid.size() > 128 || m.mime.size() > 64 || m.sha256.size() != 64)
            return Error::Malformed;

    if (d.kind == Kind::Post) {
        if (d.title.empty() || !d.root.empty() || !d.parent.empty()) return Error::Malformed;
        if (d.domains.empty() || normalizeDomains(d.domains) != d.domains) return Error::Malformed;
    } else {
        if (!d.title.empty() || !d.domains.empty() || d.body.empty()) return Error::Malformed;
        if (d.root.size() != kIdHexChars || d.parent.size() != kIdHexChars) return Error::Malformed;
        if (!isLowerHex(d.root) || !isLowerHex(d.parent)) return Error::Malformed;
    }
    return Error::None;
}

const char* describe(Error error) {
    switch (error) {
        case Error::None: return "ok";
        case Error::Malformed: return "malformed post";
        case Error::TooLarge: return "post is too large";
        case Error::InsufficientWork: return "post lacks the required proof-of-work";
        case Error::BadSignature: return "signature does not match";
        case Error::BadId: return "id does not match content";
    }
    return "unknown error";
}

// --- Signing & verification --------------------------------------------------

int workBits(const Post& p) {
    try {
        return leadingZeroBits(crypto::blake2b(concat(canonicalBytes(p), p.nonce)));
    } catch (const json::exception&) {
        return 0;
    }
}

std::optional<Post> sign(Draft draft, const identity::Persona& persona, Disclosure disclosure,
                         const std::string& alias, int powBits, Error* error) {
    auto fail = [error](Error e) -> std::optional<Post> {
        if (error) *error = e;
        return std::nullopt;
    };

    if (draft.kind == Kind::Post) draft.domains = normalizeDomains(draft.domains);
    if (Error e = validateDraft(draft); e != Error::None) return fail(e);

    Post p;
    p.content = std::move(draft);
    p.publicKey = persona.publicKey();
    p.disclosure = disclosure;
    p.alias = disclosure == Disclosure::Alias ? alias : std::string();
    p.showFingerprint = disclosure != Disclosure::Anonymous;
    p.powBits = std::max(0, powBits);
    if (Error e = validateDisclosure(p); e != Error::None) return fail(e);

    Bytes canonical;
    try {
        canonical = canonicalBytes(p);
    } catch (const json::exception&) {
        return fail(Error::Malformed);  // invalid UTF-8 in some field
    }

    // Proof-of-work: count up from a random start until the hash has enough
    // leading zero bits.
    Bytes nonce = crypto::randomBytes(kNonceBytes);
    while (leadingZeroBits(crypto::blake2b(concat(canonical, nonce))) < p.powBits) {
        for (size_t i = 0; i < nonce.size() && ++nonce[i] == 0; ++i) {}
    }
    p.nonce = std::move(nonce);
    p.signature = persona.sign(concat(canonical, p.nonce));
    p.id = computeId(canonical, p.nonce, p.signature);

    if (p.toJson().size() > kMaxEncoded) return fail(Error::TooLarge);
    if (error) *error = Error::None;
    return p;
}

Error verify(const Post& p, int minPowBits) {
    if (p.version != kFormatVersion) return Error::Malformed;
    if (p.publicKey.size() != crypto::kPublicKeyBytes) return Error::Malformed;
    if (p.signature.size() != crypto::kSignatureBytes) return Error::Malformed;
    if (p.nonce.size() != kNonceBytes) return Error::Malformed;
    if (p.id.size() != kIdHexChars || !isLowerHex(p.id)) return Error::Malformed;
    if (Error e = validateDraft(p.content); e != Error::None) return e;
    if (Error e = validateDisclosure(p); e != Error::None) return e;

    Bytes canonical;
    try {
        canonical = canonicalBytes(p);
    } catch (const json::exception&) {
        return Error::Malformed;
    }
    if (canonical.size() > kMaxEncoded) return Error::TooLarge;

    // The claimed work must meet the minimum, and the nonce must deliver it.
    if (p.powBits < minPowBits) return Error::InsufficientWork;
    if (leadingZeroBits(crypto::blake2b(concat(canonical, p.nonce))) < p.powBits)
        return Error::InsufficientWork;

    if (!crypto::verify(p.publicKey, concat(canonical, p.nonce), p.signature))
        return Error::BadSignature;
    if (computeId(canonical, p.nonce, p.signature) != p.id) return Error::BadId;
    return Error::None;
}

// --- Display & wire format ---------------------------------------------------

std::string Post::authorDisplay() const {
    const std::string fp = identity::fingerprint(publicKey);
    switch (disclosure) {
        case Disclosure::Anonymous: return "Anonymous";
        case Disclosure::Alias: return showFingerprint ? alias + " · " + fp : alias;
        case Disclosure::Persona: return fp;
    }
    return fp;
}

std::string Post::toJson() const {
    json doc = canonicalDoc(*this);
    doc["nonce"] = toBase64Url(nonce);
    doc["sig"] = toBase64Url(signature);
    doc["id"] = id;
    return doc.dump();
}

std::optional<Post> Post::fromJson(std::string_view text) {
    if (text.size() > kMaxEncoded) return std::nullopt;
    const json doc = json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object()) return std::nullopt;

    try {
        Post p;
        p.version = doc.at("v").get<int>();
        auto kind = kindFrom(doc.at("kind").get<std::string>());
        auto disclosure = identity::disclosureFrom(doc.at("disc").get<std::string>());
        auto pk = fromBase64Url(doc.at("pk").get<std::string>());
        auto nonce = fromBase64Url(doc.at("nonce").get<std::string>());
        auto sig = fromBase64Url(doc.at("sig").get<std::string>());
        if (!kind || !disclosure || !pk || !nonce || !sig) return std::nullopt;

        Draft& c = p.content;
        c.kind = *kind;
        c.forum = doc.at("forum").get<std::string>();
        c.root = doc.at("root").get<std::string>();
        c.parent = doc.at("parent").get<std::string>();
        c.title = doc.at("title").get<std::string>();
        c.body = doc.at("body").get<std::string>();
        c.domains = doc.at("domains").get<std::vector<std::string>>();
        c.timestampMs = doc.at("ts").get<int64_t>();
        for (const auto& m : doc.at("media")) {
            c.media.push_back({m.at("cid").get<std::string>(), m.at("mime").get<std::string>(),
                               m.at("size").get<uint64_t>(), m.at("sha256").get<std::string>()});
        }

        p.publicKey = std::move(*pk);
        p.disclosure = *disclosure;
        p.alias = doc.at("alias").get<std::string>();
        p.showFingerprint = doc.at("fp").get<bool>();
        p.powBits = doc.at("pow").get<int>();
        p.nonce = std::move(*nonce);
        p.signature = std::move(*sig);
        p.id = doc.at("id").get<std::string>();
        return p;
    } catch (const json::exception&) {
        return std::nullopt;
    }
}

} // namespace forumer::post