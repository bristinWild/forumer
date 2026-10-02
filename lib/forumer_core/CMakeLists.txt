# forumer_core — Forumer's engine: identity, crypto, envelopes, policy.
#
# A plain C++20 static library with no Qt and no Logos SDK, so it can be unit
# tested on its own (see tests/) and later promoted to a standalone Logos core
# module without changes to its API. The Basecamp app (src/forumer_backend.*)
# links it and adapts it to Qt/QML.
#
# Works both inside the Nix module build (included from the top-level
# CMakeLists.txt) and standalone for tests (scripts/test-core.sh).

cmake_minimum_required(VERSION 3.14)
project(forumer_core LANGUAGES CXX)

# --- libsodium ---------------------------------------------------------------
# Located by header + library rather than pkg-config, so it works in the Nix
# module build (libsodium comes in via metadata.json nix.packages) and on a
# plain developer machine alike.
if(NOT TARGET forumer::sodium)
    find_path(SODIUM_INCLUDE_DIR sodium.h REQUIRED)
    find_library(SODIUM_LIBRARY NAMES sodium libsodium REQUIRED)
    add_library(forumer::sodium UNKNOWN IMPORTED)
    set_target_properties(forumer::sodium PROPERTIES
        IMPORTED_LOCATION "${SODIUM_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${SODIUM_INCLUDE_DIR}")
endif()

# --- Library -----------------------------------------------------------------
add_library(forumer_core STATIC
    src/bytes.cpp
    src/crypto.cpp
)

target_include_directories(forumer_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(forumer_core PUBLIC cxx_std_20)

# The module plugin is a shared library, so this static archive must be PIC.
set_target_properties(forumer_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_link_libraries(forumer_core PUBLIC forumer::sodium)

# --- Tests (standalone builds only) ------------------------------------------
option(FORUMER_CORE_TESTS "Build forumer_core unit tests" OFF)
if(FORUMER_CORE_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()