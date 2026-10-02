#!/usr/bin/env bash
# Build and run forumer_core's unit tests.
#
#     ./scripts/test-core.sh
#
# Needs only Nix: cmake, a C++ compiler and libsodium are fetched from nixpkgs
# automatically. Set FORUMER_NO_NIX=1 to use a system cmake + libsodium instead.
set -euo pipefail
cd "$(dirname "$0")/.."

CMAKE_ARGS=()
if [[ -z "${FORUMER_NO_NIX:-}" ]] && command -v nix >/dev/null 2>&1; then
    # Re-run ourselves inside a shell with Nix's cmake and compiler. (Nix's
    # cmake ignores the macOS SDK, so the system AppleClang can't be mixed in.)
    if [[ -z "${FORUMER_IN_NIX_SHELL:-}" ]]; then
        export FORUMER_IN_NIX_SHELL=1
        exec nix shell nixpkgs#cmake nixpkgs#stdenv.cc -c "$0" "$@"
    fi
    # libsodium's headers (.dev) and library (.out) are separate store paths.
    SODIUM_DEV="$(nix build --no-link --print-out-paths nixpkgs#libsodium.dev)"
    SODIUM_OUT="$(nix build --no-link --print-out-paths nixpkgs#libsodium.out)"
    SODIUM_LIB=""
    for f in "$SODIUM_OUT"/lib/libsodium.dylib "$SODIUM_OUT"/lib/libsodium.so; do
        if [[ -e "$f" ]]; then SODIUM_LIB="$f"; break; fi
    done
    # nlohmann/json is header-only.
    JSON_DIR="$(nix build --no-link --print-out-paths nixpkgs#nlohmann_json)"
    echo "libsodium headers: $SODIUM_DEV/include"
    echo "libsodium library: $SODIUM_LIB"
    echo "nlohmann/json:     $JSON_DIR"
    CMAKE_ARGS+=("-DSODIUM_INCLUDE_DIR=$SODIUM_DEV/include" "-DSODIUM_LIBRARY=$SODIUM_LIB")
    CMAKE_ARGS+=("-DCMAKE_PREFIX_PATH=$JSON_DIR")
    CMAKE_ARGS+=("-DCMAKE_CXX_COMPILER=$(command -v c++)")
fi

BUILD_DIR="${BUILD_DIR:-build/core-tests}"
cmake -S lib/forumer_core -B "$BUILD_DIR" -DFORUMER_CORE_TESTS=ON -DCMAKE_BUILD_TYPE=Debug \
      ${CMAKE_ARGS[@]+"${CMAKE_ARGS[@]}"}
cmake --build "$BUILD_DIR" --parallel
ctest --test-dir "$BUILD_DIR" --output-on-failure