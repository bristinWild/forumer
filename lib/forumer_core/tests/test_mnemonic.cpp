#include "test.h"

#include <sstream>
#include <vector>

#include "forumer_core/crypto.h"
#include "forumer_core/mnemonic.h"

using namespace forumer;

namespace {

SecretBytes filled(uint8_t value) {
    SecretBytes s(32);
    for (size_t i = 0; i < s.size(); ++i) s.data()[i] = value;
    return s;
}

std::vector<std::string> wordsOf(const std::string& phrase) {
    std::istringstream in(phrase);
    std::vector<std::string> out;
    for (std::string w; in >> w;) out.push_back(w);
    return out;
}

} // namespace

// Official BIP-39 test vectors (256-bit entropy) from the reference
// implementation's vectors.json (trezor/python-mnemonic).
TEST(mnemonic_bip39_vector_zeros) {
    CHECK_EQ(mnemonic::encode(filled(0x00)),
             std::string("abandon abandon abandon abandon abandon abandon abandon abandon "
                         "abandon abandon abandon abandon abandon abandon abandon abandon "
                         "abandon abandon abandon abandon abandon abandon abandon art"));
}

TEST(mnemonic_bip39_vector_7f) {
    CHECK_EQ(mnemonic::encode(filled(0x7f)),
             std::string("legal winner thank year wave sausage worth useful legal winner "
                         "thank year wave sausage worth useful legal winner thank year wave "
                         "sausage worth title"));
}

TEST(mnemonic_bip39_vector_ff) {
    CHECK_EQ(mnemonic::encode(filled(0xff)),
             std::string("zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo "
                         "zoo zoo zoo zoo zoo zoo zoo vote"));
}

TEST(mnemonic_round_trip_random) {
    for (int i = 0; i < 50; ++i) {
        const SecretBytes secret = crypto::randomSecret(32);
        const std::string phrase = mnemonic::encode(secret);
        CHECK_EQ(wordsOf(phrase).size(), mnemonic::kWordCount);
        auto decoded = mnemonic::decode(phrase);
        CHECK(decoded.ok());
        CHECK(decoded.secret->equals(secret));
    }
}

TEST(mnemonic_decode_is_forgiving_about_formatting) {
    const SecretBytes secret = crypto::randomSecret(32);
    std::string messy;
    for (const auto& w : wordsOf(mnemonic::encode(secret))) {
        std::string upper = w;
        for (auto& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        messy += "  " + upper + "\n\t";
    }
    auto decoded = mnemonic::decode(messy);
    CHECK(decoded.ok() && decoded.secret->equals(secret));
}

TEST(mnemonic_accepts_unique_four_letter_prefixes) {
    const SecretBytes secret = crypto::randomSecret(32);
    std::string shortened;
    for (const auto& w : wordsOf(mnemonic::encode(secret)))
        shortened += w.substr(0, 4) + " ";
    auto decoded = mnemonic::decode(shortened);
    CHECK(decoded.ok() && decoded.secret->equals(secret));
}

TEST(mnemonic_rejects_wrong_word_count) {
    auto words = wordsOf(mnemonic::encode(crypto::randomSecret(32)));
    words.pop_back();
    std::string phrase;
    for (const auto& w : words) phrase += w + " ";
    CHECK(mnemonic::decode(phrase).error == mnemonic::DecodeError::WrongWordCount);
    CHECK(mnemonic::decode("").error == mnemonic::DecodeError::WrongWordCount);
}

TEST(mnemonic_reports_unknown_word_and_position) {
    auto words = wordsOf(mnemonic::encode(crypto::randomSecret(32)));
    words[4] = "forumer";
    std::string phrase;
    for (const auto& w : words) phrase += w + " ";
    auto decoded = mnemonic::decode(phrase);
    CHECK(decoded.error == mnemonic::DecodeError::UnknownWord);
    CHECK_EQ(decoded.badWord, std::string("forumer"));
    CHECK_EQ(decoded.badWordIndex, size_t(5));
}

TEST(mnemonic_checksum_catches_swapped_words) {
    // Swapping two different words keeps every word valid but breaks the
    // checksum (except in the rare case the swap happens to still check out,
    // so try a few secrets and require that most are caught).
    int caught = 0;
    for (int i = 0; i < 20; ++i) {
        auto words = wordsOf(mnemonic::encode(crypto::randomSecret(32)));
        if (words[0] == words[1]) continue;
        std::swap(words[0], words[1]);
        std::string phrase;
        for (const auto& w : words) phrase += w + " ";
        if (mnemonic::decode(phrase).error == mnemonic::DecodeError::BadChecksum) ++caught;
    }
    CHECK(caught >= 18);
}

TEST(mnemonic_encode_rejects_wrong_size) {
    CHECK(mnemonic::encode(SecretBytes(16)).empty());
}