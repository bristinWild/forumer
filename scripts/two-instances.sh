#!/usr/bin/env bash
# Run two isolated Forumer instances (separate keys, store and peer id).
set -euo pipefail
cd "$(dirname "$0")/.."
BASE="${FORUMER_TEST_DIR:-$HOME/forumer-test}"
# Each backend also logs to <instance>/module_data/forumer/backend.log
# (turned on by the debug-log file; FORUMER_LOG_FILE where the host passes it).
for i in a b; do
    mkdir -p "$BASE/$i/module_data/forumer"
    touch "$BASE/$i/module_data/forumer/debug-log"
done
LOGOS_USER_DIR="$BASE/a" FORUMER_LOG_FILE="$BASE/a/module_data/forumer/backend.log" nix run &
sleep 20   # let A start first (avoids nix eval-cache contention)
LOGOS_USER_DIR="$BASE/b" FORUMER_LOG_FILE="$BASE/b/module_data/forumer/backend.log" nix run &
wait