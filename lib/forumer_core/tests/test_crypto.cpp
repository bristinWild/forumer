#include "test.h"

#include "forumer_core/bytes.h"
#include "forumer_core/crypto.h"

using namespace forumer;

//  Encodings 

TEST(hex_round_trip) {
    const Bytes data = {0x00, 0x01, 0xab, 0xff};
    CHECK_EQ(toHex(data), std::string("0001abff"));
    CHECK(fromHex("0001abff") == data);
    CHECK(fromHex("0001ABFF") == data);
    CHECK(!fromHex("abc").has_value());   // odd length
    CHECK(!fromHex("zz").has_value());    // bad digit
}

TEST(base64url_round_trip) {
    const Bytes data = {0xfb, 0xff, 0xbf, 0x00, 0x10};
    const std::string text = toBase64Url(data);
    CHECK(text.find('+') == std::string::npos);  // URL-safe alphabet
    CHECK(text.find('/') == std::string::npos);
    CHECK(text.find('=') == std::string::npos);  // no padding
    CHECK(fromBase64Url(text) == data);
    CHECK(!fromBase64Url("not base64!").has_value());
}

TEST(secret_bytes_equality_and_clear) {
    SecretBytes a = crypto::randomSecret(32);
    SecretBytes b = a;
    CHECK(a.equals(b));
    b.data()[0] ^= 1;
    CHECK(!a.equals(b));
    a.clear();
    CHECK(a.empty());
}

//  Signatures 

TEST(signing_key_is_deterministic_from_seed) {
    const SecretBytes seed = crypto::randomSecret(crypto::kSeedBytes);
    auto a = crypto::signingKeyPairFromSeed(seed);
    auto b = crypto::signingKeyPairFromSeed(seed);
    CHECK(a.has_value() && b.has_value());
    CHECK(a->publicKey == b->publicKey);
    CHECK_EQ(a->publicKey.size(), crypto::kPublicKeyBytes);
    CHECK(!crypto::signingKeyPairFromSeed(SecretBytes(31)).has_value());
}

TEST(sign_and_verify) {
    auto kp = crypto::signingKeyPairFromSeed(crypto::randomSecret(crypto::kSeedBytes));
    const Bytes msg = bytesOf("hello forumer");
    const Bytes sig = crypto::sign(kp->secretKey, msg);
    CHECK_EQ(sig.size(), crypto::kSignatureBytes);
    CHECK(crypto::verify(kp->publicKey, msg, sig));

    Bytes tampered = msg;
    tampered[0] ^= 1;
    CHECK(!crypto::verify(kp->publicKey, tampered, sig));

    auto other = crypto::signingKeyPairFromSeed(crypto::randomSecret(crypto::kSeedBytes));
    CHECK(!crypto::verify(other->publicKey, msg, sig));
    CHECK(!crypto::verify(Bytes(5), msg, sig));  // wrong-size key just fails
}

//  Hashing & KDF 

TEST(blake2b_known_vector) {
    // BLAKE2b-256("abc"), from the BLAKE2 reference implementation.
    CHECK_EQ(toHex(crypto::blake2b(bytesOf("abc"))),
             std::string("bddd813c634239723171ef3fee98579b94964e3bb1cb3e427262c8c068d52319"));
    CHECK(crypto::blake2b(bytesOf("x"), 8).empty());  // below minimum length
}

TEST(kdf_subkeys_are_deterministic_and_independent) {
    const SecretBytes root = crypto::randomSecret(32);
    auto k0 = crypto::deriveSubkey(root, 0, "frmrpers");
    auto k0again = crypto::deriveSubkey(root, 0, "frmrpers");
    auto k1 = crypto::deriveSubkey(root, 1, "frmrpers");
    auto otherCtx = crypto::deriveSubkey(root, 0, "frmrdmky");
    CHECK(k0 && k0again && k1 && otherCtx);
    CHECK(k0->equals(*k0again));
    CHECK(!k0->equals(*k1));
    CHECK(!k0->equals(*otherCtx));
    CHECK(!crypto::deriveSubkey(root, 0, "short").has_value());  // context must be 8 chars
}

//  Password hashing & AEAD 

TEST(password_key_depends_on_password_and_salt) {
    const auto params = crypto::interactivePasswordHashParams();
    const Bytes salt = crypto::randomBytes(crypto::kPwSaltBytes);
    auto a = crypto::deriveKeyFromPassword("correct horse", salt, params);
    auto b = crypto::deriveKeyFromPassword("correct horse", salt, params);
    auto c = crypto::deriveKeyFromPassword("wrong horse", salt, params);
    auto d = crypto::deriveKeyFromPassword("correct horse", crypto::randomBytes(16), params);
    CHECK(a && b && c && d);
    CHECK(a->equals(*b));
    CHECK(!a->equals(*c));
    CHECK(!a->equals(*d));
    CHECK(!crypto::deriveKeyFromPassword("x", Bytes(3), params).has_value());
}

TEST(aead_round_trip_and_tamper_detection) {
    const SecretBytes key = crypto::randomSecret(crypto::kAeadKeyBytes);
    const Bytes plain = bytesOf("the master secret");
    const Bytes ad = bytesOf("forumer-vault-v1");

    auto sealed = crypto::aeadEncrypt(key, plain.data(), plain.size(), ad);
    CHECK(sealed.has_value());
    CHECK_EQ(sealed->ciphertext.size(), plain.size() + crypto::kAeadTagBytes);

    auto opened = crypto::aeadDecrypt(key, *sealed, ad);
    CHECK(opened.has_value());
    CHECK(Bytes(opened->data(), opened->data() + opened->size()) == plain);

    auto bad = *sealed;
    bad.ciphertext[0] ^= 1;
    CHECK(!crypto::aeadDecrypt(key, bad, ad).has_value());                   // tampered
    CHECK(!crypto::aeadDecrypt(key, *sealed, bytesOf("other")).has_value()); // wrong AD
    CHECK(!crypto::aeadDecrypt(crypto::randomSecret(32), *sealed, ad).has_value()); // wrong key

    // Same plaintext twice -> different nonces and ciphertexts.
    auto again = crypto::aeadEncrypt(key, plain.data(), plain.size(), ad);
    CHECK(again->nonce != sealed->nonce);
}