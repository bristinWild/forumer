# E3 — multi-node and UI integration verification (v0.2.5)

## Scope and status

E3's agreed **public and private peer-integration scope passed** on the `hardening/v0.2.5` branch in manual GitHub Actions run **E3 Two-Node Network Integration #2** (7m49s). Headless QML smoke checks and local cross-profile tests have also been exercised. This does **not** mean every offline failure mode, Testnet 0.4 runtime, or security property is proven.

The manual workflow contacts shared `logos.dev`. Running it creates synthetic public traffic on a real network, not a private emulator.

## Test 1 — Headless UI

File: `tests/ui-tests.mjs`. Workflow: `.github/workflows/e3-ui.yml`. Build/test command:

```bash
nix build .#integration-test -o result-e3 -L
cat result-e3/integration-test.log
```

Checks startup/onboarding and basic UI/navigation contracts. It does not prove a peer exchange.

## Test 2 — Public two-node sync

File: `tests/test-e3-two-node.py`. Workflow: `.github/workflows/e3-two-node-manual.yml`.

The test starts independent temporary nodes with separate `LOGOS_USER_DIR` profiles and **synthetic** identities. It publishes a signed topic from A, checks its identity/content on B, posts a reply from B and verifies A receives it, then restarts B to test persistence and catch-up. It runs backend identifier-log checks. PASS here demonstrates this specific send/receive/restart scenario—not every transport outage behavior.

## Test 3 — Private forum isolation

File: `tests/test-e3-private.py`, launched by the same manual workflow.

The test creates synthetic identities A/B/C; A creates a private forum and B joins with its invite. B receives the same private post and decrypted content. Non-member C cannot access it. The test checks local backend logs for long identifier leakage. It does not prove all traffic-analysis protections or invite revocation (neither is provided by private forums).

## Deferred sender outbox scenario

An optional injected transport-failure test was attempted but never produced a verified result; experimental production hooks were reverted. The release must not claim that a **sender's unsent post survived forced transport failure + restart** based solely on the receiver catch-up scenario above. Existing outbox code and lower-level persistence tests are separate evidence.

## Privacy requirements for test operators

Use synthetic accounts only. Do not reuse personal identities or private invitation links. Do not attach full host logs, profile files, recovery phrases, invite secrets or unfiltered CI traces to a PR. The workflow retains only a seven-day summary **artifact**; Actions' step logs can persist under a different policy.

## Remaining release acceptance tests

- Official Logos Testnet 0.4 peer-to-peer end-to-end (not verified).
- Windows native Basecamp load/UI/runtime (not verified).
- Full runtime MediaAsk flood/queued previews (not verified).
- Sender-offline outbox fault-injection (deferred).

For the full test matrix, see [testing.md](testing.md).
