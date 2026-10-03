#!/usr/bin/env bash
# Build and run forumer_core's unit tests.
#
#     ./scripts/test-core.sh
#
# Needs only Nix: cmake, a C++ compiler, libsodium, SQLite and nlohmann/json are
# fetched from nixpkgs automatically. Set FORUMER_NO_NIX=1 to use the system's
# instead.
set -euo pipefail
cd "$(dirname "$0")/.."

# Path of lib<name>.dylib / lib<name>.so inside a Nix store output, or "".
find_lib() {
    local dir="$1" name="$2" f
    for f in "$dir/lib/lib$name.dylib" "$dir/lib/lib$name.so"; do
        if [[ -e "$f" ]]; then echo "$f"; return; fi
    done
    echo ""
}

CMAKE_ARGS=()
if [[ -z "${FORUMER_NO_NIX:-}" ]] && command -v nix >/dev/null 2>&1; then
    # Re-run ourselves inside a shell with Nix's cmake and compiler. (Nix's
    # cmake ignores the macOS SDK, so the system AppleClang can't be mixed in.)
    if [[ -z "${FORUMER_IN_NIX_SHELL:-}" ]]; then
        export FORUMER_IN_NIX_SHELL=1
        exec nix shell nixpkgs#cmake nixpkgs#stdenv.cc -c "$0" "$@"
    fi
    # Headers (.dev) and libraries (.out) are separate store paths.
    SODIUM_DEV="$(nix build --no-link --print-out-paths nixpkgs#libsodium.dev)"
    SODIUM_LIB="$(find_lib "$(nix build --no-link --print-out-paths nixpkgs#libsodium.out)" sodium)"
    SQLITE_DEV="$(nix build --no-link --print-out-paths nixpkgs#sqlite.dev)"
    SQLITE_LIB="$(find_lib "$(nix build --no-link --print-out-paths nixpkgs#sqlite.out)" sqlite3)"
    # nlohmann/json is header-only.
    JSON_DIR="$(nix build --no-link --print-out-paths nixpkgs#nlohmann_json)"

    echo "libsodium: $SODIUM_DEV/include  $SODIUM_LIB"
    echo "sqlite:    $SQLITE_DEV/include  $SQLITE_LIB"
    echo "json:      $JSON_DIR"
    CMAKE_ARGS+=("-DSODIUM_INCLUDE_DIR=$SODIUM_DEV/include" "-DSODIUM_LIBRARY=$SODIUM_LIB")
    CMAKE_ARGS+=("-DSQLite3_INCLUDE_DIR=$SQLITE_DEV/include" "-DSQLite3_LIBRARY=$SQLITE_LIB")
    CMAKE_ARGS+=("-DCMAKE_PREFIX_PATH=$JSON_DIR")
    CMAKE_ARGS+=("-DCMAKE_CXX_COMPILER=$(command -v c++)")
fi

BUILD_DIR="${BUILD_DIR:-build/core-tests}"
cmake -S lib/forumer_core -B "$BUILD_DIR" -DFORUMER_CORE_TESTS=ON -DCMAKE_BUILD_TYPE=Debug \
      ${CMAKE_ARGS[@]+"${CMAKE_ARGS[@]}"}
cmake --build "$BUILD_DIR" --parallel
ctest --test-dir "$BUILD_DIR" --output-on-failure