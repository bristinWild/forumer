#!/usr/bin/env bash
# Run two isolated Forumer instances (separate keys, store and peer id).
set -euo pipefail
cd "$(dirname "$0")/.."
BASE="${FORUMER_TEST_DIR:-$HOME/forumer-test}"
LOGOS_USER_DIR="$BASE/a" nix run &
sleep 20   # let A start first (avoids nix eval-cache contention)
LOGOS_USER_DIR="$BASE/b" nix run &
wait