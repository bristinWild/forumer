#include "test.h"

#include "forumer_core/crypto.h"

int main() {
    if (!forumer::crypto::initCrypto()) {
        std::cerr << "libsodium failed to initialise\n";
        return 2;
    }

    int failedCases = 0;
    for (const auto& c : forumer_test::registry()) {
        const int before = forumer_test::failures();
        c.fn();
        const bool ok = forumer_test::failures() == before;
        if (!ok) ++failedCases;
        std::cout << (ok ? "  ok   " : "  FAIL ") << c.name << "\n";
    }

    const size_t total = forumer_test::registry().size();
    std::cout << "\n" << (total - failedCases) << "/" << total << " tests passed\n";
    return failedCases == 0 ? 0 : 1;
}