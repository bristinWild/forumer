# Forumer test matrix and release evidence — v0.2.5

This document separates **source-level checks**, **unit tests**, **integration runs**, **cross-builds**, and **real runtime validation**. Passing one category does not prove another. Source: `hardening/v0.2.5`, 2026-10-18.

## Test and build commands

| Test | Command | What it checks |
|---|---|---|
| C++ core | `./scripts/test-core.sh` | Post validation, identity, encrypted persistence, sync, flood gates, forums. |
| Markdown | `node scripts/test-markdown.mjs` | QML content rendering sanitization. |
| Links | `node scripts/test-links.mjs` | Invite, intent, and external link validation. |
| Phase A handler | `node scripts/test-phase-a-handler.mjs` | Source ordering of MediaAsk token budget before preview, queued responses and Storage exists call. |
| Log safety | `node scripts/test-log-safety.mjs` | Scrubber/logging shape, three A5 sensitive diagnostic patterns, identifier-leak detector behavior. |
| Image pipeline | `./scripts/test-image-pipeline.sh` | PNG/JPEG processing, resize, metadata stripping and pixel guard. |
| Headless view | `nix build .#integration-test -L` | QML launch/navigation smoke tests. |
| Local module | `nix build -L` | Native Logos module build. |
| Standalone | `nix build .#ui-dev -L` | Standalone Forumer/host launcher build. |
| Windows cross-compile | `nix build .#packages.x86_64-windows.lib -L` | Windows DLL cross-build (preferred CI host is Ubuntu). |
| Windows package | `nix build .#packages.x86_64-windows.lgx -L` | Windows LGX packaging, not an in-Basecamp install test. |

## Automated CI workflows

- `.github/workflows/ci.yml` — core, JS safety guards, image pipeline and native module builds on Linux/macOS.
- `.github/workflows/e3-ui.yml` — headless QML smoke suite.
- `.github/workflows/e3-two-node-manual.yml` — **manual**, shared `logos.dev` network; synthetic accounts only; two-node public topic/reply/restart sync and three-node private-forum sync/non-member isolation; only short-lived summary artifact (seven days). **Never treat this as isolated network CI.**
- `.github/workflows/windows-build.yml` — **manual**, Linux-hosted MinGW cross-build, DLL check and Windows LGX build; seven-day summary, not distributable binary artifact.
- `.github/workflows/release.yml` — on `v*` tag, package and attach Linux x86_64, macOS arm64, and Windows x86_64 LGX plus SHA256 files; Windows runtime remains **unverified**.

## Evidence available at documentation time

| Area | Status | Qualification |
|---|---|---|
| A1 digest flood | Pass, with pre-fix regression | Budget checked before held-set scan. |
| A2 empty range cooldown | Pass, with pre-fix regression | Cooldown charged only when an answer is issued. |
| A3 MediaAsk | Partial | Token bucket, static ordering guard; no complete handler/queued-callback flood stress run. |
| A5 log safety | Improved, tests/builds green | New sanitization at three call sites and CI regression checks; not full-host output audit. |
| E2 image pipeline | Pass (PNG/JPEG) | WebP CI decode unverified. |
| E3 public multi-node | Pass on shared-network manual Actions #2 | Signed topic, cross-peer reply, restart/catch-up. |
| E3 private forum | Pass on shared-network manual Actions #2 | Three independent synthetic identities, member sync and non-member isolation. |
| Linux/macOS module builds | Pass on CI | Platform build is not release installer acceptance testing. |
| Windows x86_64 cross-build | Pass on manual Actions #1 | DLL and LGX build, **not** native Windows/Basecamp runtime test. |
| Official Logos Testnet 0.4 | Not verified | Must be done on official testnet if required for prize/release. |
| Windows distribution in release workflow | Packaged | Windows x86_64 LGX is included in the tag workflow; native Windows Basecamp install/runtime remains unverified. |
| Basecamp image-provider PR #514 | Not merged at review time | Target Basecamp must include required image-provider patch. |
| Sender transport-failure persistence | Deferred | An explicit injected sender-offline test was not completed. |

## E3 security posture

The manual E3 workflow publishes **synthetic** content to a **shared** network, so content is not guaranteed ephemeral. It does not publish real personas or invites. It uploads a short result summary, not test profile directories or raw backend logs. GitHub Actions **job logs** have their own retention settings: a seven-day artifact limit does not imply seven-day job-log retention. Review any failed job logs before sharing; sanitize source paths, addresses, invites, and full identifiers.

## How to record a release verification

Document: date/UTC, tested commit SHA, Basecamp build/version, Delivery and Storage versions, network preset, platform/architecture, test command or CI run URL, observed pass/fail and known gaps. For a private test, use synthetic one-time keys. Never publish actual recovery phrases, private-forum invite links, unredacted account state or profiles.
