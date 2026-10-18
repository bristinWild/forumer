# Forumer architecture and protocol guide (v0.2.5 candidate)

> Implementation reference: [`hardening/v0.2.5`](https://github.com/bristinWild/forumer/tree/hardening/v0.2.5), reviewed 2026-10-18. This is an implementation guide, not a claim that all platform/runtime scenarios have been verified. For threat coverage, see [security.md](security.md); for evidence, see [testing.md](testing.md).

## What Forumer is

Forumer is a peer-to-peer discussion application implemented as a Logos Basecamp `ui_qml` module. Users write topics and replies without creating a server account, email identity, or phone-number profile. Posts are signed locally and exchanged over Logos Delivery. Image blobs are processed locally and exchanged through Logos Storage. Private-forum traffic is encrypted under keys distributed through invitation links.

**Privacy is not anonymity at the transport layer.** A network observer can correlate the node's network address, connection metadata, and publication timing. Public posts and signed keys remain observable and may be stored by peers indefinitely. A private-forum invite is a bearer secret; anyone holding it can participate.

## Layered architecture

```text
Basecamp QML view (src/qml/*.qml)
    | ForumStore.qml, views, markdown.js, links.js
    v
Qt Remote Objects contract (src/forumer.rep)
    |
    v
ForumerBackend (src/forumer_backend.{h,cpp})
    | identity, transport/session lifecycle, timers, reconciliation,
    | media upload/download, API state, persistence orchestration
    +------------------------> DeliveryModuleTransport
    |                           (src/delivery_module_transport.*)
    |                               |
    |                               v
    |                           Logos Delivery
    +------------------------> Logos Storage API
    |                               |
    |                               v
    |                           image/media blobs
    v
forumer_core (lib/forumer_core/)
    identity, mnemonic, vault, account store, posts, signatures,
    sync codec, digests, history planner, flood gates, private forums,
    encrypted media, local PostStore (SQLite)
```

`forumer_core` is a reusable C++ library inside the module, not another Logos module. The QML front end and backend communicate through generated Qt Remote Objects interfaces. Source-of-truth API declarations live in `src/forumer.rep` and `src/forumer_backend.h`.

## Core responsibilities by file

| Path | Responsibility |
|---|---|
| `lib/forumer_core/include/forumer_core/post.h` | Canonical post envelope, content limits, PoW, signature and content-ID validation. |
| `identity.h`, `mnemonic.h`, `vault.h`, `account_store.h` | Account master secrets, derivable personas, recovery, encrypted local account storage. |
| `sync.h`, `digest_plan.h`, `thread.h` | Delivery envelopes, reconciliation, range/history planning, replies. |
| `flood.h` | Admission and computational budgets for posts and unsigned sync requests. |
| `forums.h`, `sealed.h` | Private-forum key derivation, authenticated encryption of network messages and local records. |
| `post_store.h` | Local persistent post log, queryable threads, previews, and outbox state. |
| `media.h`, `src/image_prep.*` | Image preparation, authenticated media references, previews and storage interactions. |
| `src/delivery_module_transport.*` | Adapter between Forumer's sync protocol and Delivery module APIs. |
| `src/qml/ForumStore.qml` | Client-side state and backend interface wiring. |
| `src/qml/links.js`, `markdown.js` | Validated deep-link processing and safe text formatting. |

## Identities and post authentication

Each account has a locally generated master secret, protected at rest by a password-derived encryption key. A 24-word recovery phrase represents that secret. Persona keys are deterministically derived from it; a user can keep, manually rotate, or automatically rotate personas. A post can be displayed under a visible persona fingerprint, an alias, or as “Anonymous.” Display choice does **not** mean an unsigned post: the protocol includes a verifiable signing key even for anonymous disclosure.

A post is an immutable signed envelope. `post.h` describes the flow: canonicalize the signed fields, find a proof-of-work nonce, sign the canonical data and nonce with Ed25519, then derive a content ID from the signed message. Verification checks the payload shape/size, work, signature and ID. The 32-hex post ID identifies *content* and is not an account identifier. A retry of the same post retains its ID.

Limits defined in `post.h`: 200-byte title, 10,000-byte body, up to three domains, up to four image references, and 16 KiB maximum encoded post envelope. Author-controlled timestamps are not trusted as network timing evidence.

## Public and private forums

The public forum uses shared Delivery content topics. Private forums derive an opaque topic and distinct wire/storage sealing keys from a randomly generated 32-byte forum key; see `forums.h`. Private messages on that topic are authenticated-encrypted with XChaCha20-Poly1305. Their contents remain signed, and ordinary post verification and flood controls still apply after decryption. Private-forum post envelopes are also sealed at rest in the local post log.

**Invitation security:** the invitation contains the forum key. Treat it like a password; copying an invitation into a public chat gives anyone access. There is no central administrator, membership-revocation mechanism, or retrospective deletion. If the key leaks, create a replacement forum and distribute its new key privately.

## Delivery, sync and offline operation

The backend publishes signed posts and periodically advertises held posts through digest messages. Peers compare digests and resend missing content subject to admission limits and resend budgets. Range requests allow history recovery, while Delivery store-node queries help reconstruct older content. A previously offline receiver can restart and catch up; outbox entries represent locally authored posts awaiting confirmation. Sender-offline outbox persistence after a deliberately injected transport failure was not exercised by the automated two-node suite and remains a separate validation gap.

A request or digest is not evidence that a peer accepted an object until corresponding receipt/confirmation logic completes. Do not equate QML “sending”/“live” labels alone with cross-node integration: E3 tests compare actual post IDs, content, replies and persisted state.

## Images and Logos Storage

Attached images are locally decoded, resized/processed, re-encoded to strip metadata, and uploaded to Storage; the signed post carries a media reference (CID, MIME, size and checksum), not inline image bytes. Viewers load images on demand and verify the downloaded content against the author's signed reference. Private-forum media are sealed with a forum-dependent key. Preview transfer and MediaAsk discovery are rate-budgeted. Re-hosting can be disabled.

PNG/JPEG pipeline behavior is verified in CI; WebP decode compatibility is not. Image display in Basecamp depends on the upstream sandbox-safe image-provider fix (Basecamp PR #514), not solely on the Forumer module build. The execution cost of `storage_module.exists()` has not been benchmarked.

## Flood-control layers

`flood.h` defines complementary protections: sender-side account quotas, receiver-side per-persona limits, arrival-time and historical 24-hour bounds, shared budgets for novel keys, higher-work priority lanes, capped resend answers, a digest-scan budget, and a MediaAsk budget.

- Standard post work: 16 leading-zero bits; anonymous: 20 bits. Work is **cost per post**, not a verified sender identity or an absolute rate limit.
- Topics/replies: 10/60 per persona or honest sender account per hour; receiver arrival-time guard multiplies hourly limits by two, with additional 24-hour history caps.
- Novel keys: 60/minute with 120 burst, plus a separate high-work priority lane.
- Digest held-set reads: 60/minute, burst 120. MediaAsk work: 30/minute, burst 60. Resend limit: 240/minute, burst 128.

These quotas are *local per peer*, not network-level transport throttling. A3's actual backend flood/queued-response scenario has not been dynamically tested; existing assertions cover budget accounting and source ordering.

## Deployment and compatibility

`metadata.json` declares Forumer **v0.2.5**, type `ui_qml`, runtime Delivery dependency `~0.4.0`, Storage `~3.0.0`, and `forumer.open` intent. The Nix flake uses logos-module-builder `0.3.2`, with Delivery **v0.3.0 as a build-time interface input** and Storage `v3.0.0` as a build input. Do not confuse Nix build-time inputs with the versions installed in a target Basecamp runtime.

The regular release workflow currently builds installable portable LGX packages for **Linux x86_64** and **macOS arm64**. A separate, manually triggered workflow cross-builds **Windows x86_64** DLL and LGX on Ubuntu; a green cross-build is not evidence of Windows Basecamp runtime support, nor does the regular release workflow currently attach a Windows asset.

## Where to go next

- [Testing evidence and commands](testing.md)
- [Security threat model](security.md)
- [E3 integration procedures](e3-testing.md)
- [v0.2.5 release candidate](releases/v0.2.5.md)
- [Repository README](../README.md)
