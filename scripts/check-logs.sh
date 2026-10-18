#!/usr/bin/env bash
# Fails if a backend log carries a full identifier (0.2.4, A5).
#
#     ./scripts/check-logs.sh path/to/backend.log
#
# Every log line is scrubbed on the way out (forumer_core/redact.h), so any
# run of 20 or more letters and digits is a leak. Shortened references are 8
# characters, so they never match.
#
# Exit codes: 0 = clean, 1 = full identifiers found, 2 = the log couldn't be read.
set -euo pipefail
log="${1:?usage: check-logs.sh path/to/backend.log}"
if [[ ! -f "$log" ]]; then
    echo "FAIL: no such log: $log" >&2
    exit 2
fi
status=0
grep -Eq '[A-Za-z0-9]{20,}' "$log" || status=$?
case "$status" in
    0) echo "FAIL: full identifiers in $log (lines above)" >&2; exit 1 ;;
    1) echo "ok: no full identifiers in $log" ;;
    *) echo "FAIL: could not read $log (grep exit $status)" >&2; exit 2 ;;
esac