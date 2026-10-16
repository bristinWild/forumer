#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

# Enter a reproducible Nix environment.
if command -v nix >/dev/null && [[ -z "${FORUMER_IMAGE_IN_NIX:-}" ]]; then
    export FORUMER_IMAGE_IN_NIX=1

    packages=(
        nixpkgs#cmake
        nixpkgs#stdenv.cc
        nixpkgs#qt6.qtbase.dev
        nixpkgs#qt6.qtbase.out
    )

    # Linux Qt6Gui requires GLVND OpenGL headers/libraries.
    if [[ "$(uname -s)" == Linux ]]; then
        packages+=(nixpkgs#libglvnd.dev nixpkgs#libglvnd.out)
    fi

    exec nix shell "${packages[@]}" -c bash "$0"
fi

cmake_args=(-DCMAKE_BUILD_TYPE=Debug)

if command -v nix >/dev/null; then
    QT_DEV="$(nix build --no-link --print-out-paths nixpkgs#qt6.qtbase.dev)"
    cmake_args+=("-DCMAKE_PREFIX_PATH=$QT_DEV")

    if [[ "$(uname -s)" == Linux ]]; then
        GL_DEV="$(nix build --no-link --print-out-paths nixpkgs#libglvnd.dev)"
        GL_OUT="$(nix build --no-link --print-out-paths nixpkgs#libglvnd.out)"

        find_gl_lib() {
            local name="$1" path
            for path in "$GL_DEV/lib/lib${name}.so" "$GL_OUT/lib/lib${name}.so"; do
                if [[ -e "$path" ]]; then
                    printf '%s\n' "$path"
                    return 0
                fi
            done
            echo "Missing lib${name}.so in GLVND outputs" >&2
            return 1
        }

        cmake_args+=(
            "-DCMAKE_PREFIX_PATH=$QT_DEV;$GL_DEV;$GL_OUT"
            "-DOPENGL_INCLUDE_DIR=$GL_DEV/include"
            "-DOPENGL_GLX_INCLUDE_DIR=$GL_DEV/include"
            "-DOPENGL_opengl_LIBRARY=$(find_gl_lib OpenGL)"
            "-DOPENGL_glx_LIBRARY=$(find_gl_lib GLX)"
        )
    fi
fi

cmake -S tests/image-pipeline -B build/image-pipeline \
    "${cmake_args[@]}"

cmake --build build/image-pipeline --parallel

QT_QPA_PLATFORM=offscreen \
    ctest --test-dir build/image-pipeline --output-on-failure
