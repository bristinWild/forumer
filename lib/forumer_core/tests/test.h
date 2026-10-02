#pragma once

// A deliberately tiny test runner - no external dependency, so the core tests
// build anywhere libsodium does.
//
//   TEST(name) { CHECK(cond); CHECK_EQ(a, b); }
//
// Every TEST registers itself; test_main.cpp runs them all and exits non-zero
// if any CHECK failed.

#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace forumer_test {

struct Case {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

inline int& failures() {
    static int count = 0;
    return count;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) {
        registry().push_back({name, std::move(fn)});
    }
};

} // namespace forumer_test

#define FT_CONCAT_INNER(a, b) a##b
#define FT_CONCAT(a, b) FT_CONCAT_INNER(a, b)

#define TEST(name)                                                                  \
    static void FT_CONCAT(test_, name)();                                           \
    static ::forumer_test::Registrar FT_CONCAT(reg_, name)(#name, FT_CONCAT(test_, name)); \
    static void FT_CONCAT(test_, name)()

#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (!(cond)) {                                                              \
            std::cerr << "  FAIL " << __FILE__ << ":" << __LINE__ << "  " #cond "\n"; \
            ++::forumer_test::failures();                                           \
        }                                                                           \
    } while (0)

#define CHECK_EQ(a, b)                                                              \
    do {                                                                            \
        if (!((a) == (b))) {                                                        \
            std::cerr << "  FAIL " << __FILE__ << ":" << __LINE__ << "  " #a " == " #b "\n"; \
            ++::forumer_test::failures();                                           \
        }                                                                           \
    } while (0)
