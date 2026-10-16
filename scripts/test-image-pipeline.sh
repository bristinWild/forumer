#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

if command -v nix >/dev/null && [[ -z "${FORUMER_IMAGE_IN_NIX:-}" ]]; then
    export FORUMER_IMAGE_IN_NIX=1
    exec nix shell \
      nixpkgs#cmake \
      nixpkgs#stdenv.cc \
      nixpkgs#qt6.qtbase.dev \
      nixpkgs#qt6.qtbase.out \
      -c bash "$0"
fi

if command -v nix >/dev/null; then
    QT_DEV="$(nix build --no-link --print-out-paths nixpkgs#qt6.qtbase.dev)"
    export CMAKE_PREFIX_PATH="$QT_DEV${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
fi

cmake -S tests/image-pipeline -B build/image-pipeline \
  -DCMAKE_BUILD_TYPE=Debug

cmake --build build/image-pipeline --parallel

QT_QPA_PLATFORM=offscreen \
  ctest --test-dir build/image-pipeline --output-on-failure
