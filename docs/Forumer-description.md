# Forumer

**A private, serverless discussion forum for Logos Basecamp.**

Forumer lets people discuss any topic — politics, privacy, campus life, technology — without a central server, without an account tied to their real identity, and without a single profile that links everything they say. It runs entirely on the Logos stack: **Logos Delivery** for messaging, **Logos Storage** for media and history, and **Basecamp** as the app host.

Built as a submission for [λPrize LP-0026: Forum App](https://github.com/logos-co/lambda-prize/blob/master/prizes/LP-0026.md). Forked from and inspired by [jzaki/forum-sample-app](https://github.com/jzaki/forum-sample-app).

> **Status: in development.** This README is the design reference for building Forumer. Sections describe the target system; anything marked **⚠ verify** depends on behaviour of the Logos stack that has not yet been confirmed in testing.

---

## Table of contents

1. [What Forumer does](#1-what-forumer-does)
2. [LP-0026 requirements map](#2-lp-0026-requirements-map)
3. [User experience](#3-user-experience)
4. [Architecture](#4-architecture)
5. [Identity and privacy model](#5-identity-and-privacy-model)
6. [Data model](#6-data-model)
7. [Wire format](#7-wire-format)
8. [Network layout (content topics)](#8-network-layout-content-topics)
9. [Reliability: outbox, catch-up, gap repair](#9-reliability-outbox-catch-up-gap-repair)
10. [Anti-flood and spam resistance](#10-anti-flood-and-spam-resistance)
11. [Private forums](#11-private-forums)
12. [Persona DMs](#12-persona-dms)
13. [Media](#13-media)
14. [Search, hide and mute](#14-search-hide-and-mute)
15. [Threat model](#15-threat-model)
16. [Repository structure](#16-repository-structure)
17. [Build, run and install](#17-build-run-and-install)
18. [Testing and CI](#18-testing-and-ci)
19. [Roadmap](#19-roadmap)
20. [Out of scope and future work](#20-out-of-scope-and-future-work)
21. [Open questions and risks](#21-open-questions-and-risks)
22. [Adoption plan](#22-adoption-plan)
23. [Credits and license](#23-credits-and-license)

---

## 1. What Forumer does

| Feature | In one line |
|---|---|
| **Master identity** | One secret per account, password-protected on disk, never shown publicly. |
| **Personas** | Public identities derived from the master. Others only ever see personas. |
| **Identity rotation** | *Keep* one persona, rotate *Manually*, or *Auto* — a fresh persona for every post. |
| **Disclosure choice per post** | Post under your persona, under an alias, or fully anonymously. |
| **Multiple accounts** | Hold several master identities and switch between them. |
| **Domains** | Posts are tagged with up to 3 domains — prebuilt or typed in, comma-separated. |
| **Threads** | Posts → comments → nested replies. |
| **Feed** | Two columns: *All domains* and *Followed domains*. |
| **Search** | Full-text search over everything synced to your device. |
| **Media** | Images/files stored in Logos Storage, referenced by CID, metadata stripped. |
| **My posts** | Only you can see which posts are yours. |
| **Post status** | *Sending… / Live / Failed to publish* with Retry. |
| **Missed tab** | *Outbox* (unsent posts, Retry) and *Catch-up missed* (Load). |
| **Auto gap repair** | Missed replies are detected and recovered while you are online. |
| **Private forums** | Invite-link forums; content encrypted, unreadable without the link. |
| **Persona DMs** | Message a persona privately; neither side learns the other's master identity. |
| **Backup & restore** | Recovery phrase restores identities and *My posts* on a new device. |
| **Hide & mute** | Personal, local moderation. |
| **Anti-flood** | Size caps, rate limits, proof-of-work stamps, network RLN. |

---

## 2. LP-0026 requirements map

| LP requirement | How Forumer meets it | Where |
|---|---|---|
| Realistic topics & organic conversations | University privacy-tech seminars + sustained use; aggregate, non-deanonymizing evidence | [§22](#22-adoption-plan) |
| No centralised server or service | Peer-to-peer over Logos Delivery/Storage; no Forumer-operated server | [§4](#4-architecture) |
| Uses the Logos stack | Delivery (posts, DMs, SDS, store queries), Storage (media, snapshots) | [§8](#8-network-layout-content-topics), [§9](#9-reliability-outbox-catch-up-gap-repair) |
| One or more accounts | Account switcher; each account = one master identity | [§5](#5-identity-and-privacy-model) |
| Create topics, reply to existing ones | Posts with domains; comments; nested replies | [§6](#6-data-model) |
| Reply via unique id / alias (+ optional uid) / no id | Composer disclosure: *Persona*, *Alias* (+ optional fingerprint), *Anonymous* | [§5.4](#54-disclosure-per-post) |
| Privacy implications of fixed vs rotating identity | Keep / Manual / Auto rotation, in-app explanations, threat model | [§5.3](#53-rotation-policy), [§15](#15-threat-model) |
| Basecamp GUI, local build instructions | `ui_qml` module; build guide below | [§17](#17-build-run-and-install) |
| Pre-built in a Logos module catalog | Own fork of `logos-modules-release-base` | [§17.5](#175-module-catalog) |
| Usable by a non-expert; clear error/pending states | Guided onboarding, status chips, offline banner, Missed tab | [§3](#3-user-experience) |
| Sound, reusable architecture | `forumer_core` (reusable engine) separate from `forumer_ui` | [§4](#4-architecture) |
| Obtain past messages after being offline | Automatic catch-up on start + manual Load (store nodes + Storage snapshots) | [§9.2](#92-catch-up-after-being-offline) |
| Failed sends stay available to retry | Persistent outbox with Retry | [§9.1](#91-outbox) |
| Does not flood the network | Single shared channel, caps, rate limits, PoW, RLN | [§10](#10-anti-flood-and-spam-resistance) |
| CI green on default branch | GitHub Actions: build + unit tests | [§18](#18-testing-and-ci) |
| README with deployment & usage | This document | — |
| "Program addresses" | **N/A — Forumer deploys no on-chain programs** (blockchain is out of scope for LP-0026). Content topics and catalog URL are listed instead. | [§8](#8-network-layout-content-topics), [§17.5](#175-module-catalog) |
| Narrated video demo | Covers setup, all core flows, failure handling, design choices | [§19](#19-roadmap) |
| FURPS self-assessment | In the solution PR, per the λPrize template | [§19](#19-roadmap) |
| MIT + Apache-2.0 dual license | `LICENSE-MIT`, `LICENSE-APACHE-v2` | [§23](#23-credits-and-license) |

---

## 3. User experience

### 3.1 First run

1. Install Forumer from the module catalog in Basecamp (or build locally, [§17](#17-build-run-and-install)).
2. **Create master identity** → choose a password.
3. **Save your recovery phrase.** The screen states plainly: *forget the password and lose the phrase, and the identity is gone.*
4. Land on **Home**.

On later starts the app asks for the password to unlock the identity.

### 3.2 Screens

```
┌───────────────────────────────────────────────────────────────────┐
│  Forumer        [ search…                       ]   (●) Connected  │  ← top bar + offline banner
├───────────────────────────────┬───────────────────────────────────┤
│  ALL DOMAINS                  │  FOLLOWED DOMAINS                 │  ← Home: two columns
│  privacy   (128)  [Follow]    │  privacy                          │
│  politics  (94)   [Follow]    │   ▸ Is Aadhaar-linked SIM …  12💬 │
│  campus    (41)   [Follow]    │   ▸ ZK proofs for voting?     4💬 │
│  …                            │  campus                           │
└───────────────────────────────┴───────────────────────────────────┘
  Profile menu: My posts · Chat · Missed · Backup & restore · Settings · Switch account
```

| Screen | Contents |
|---|---|
| **Home** | Two columns: *All domains* (sorted by activity, Follow button) and *Followed domains* (feed). |
| **Domain view** | Posts tagged with that domain, newest/most active first. |
| **Thread view** | Post, comments, nested replies; reply box at each level; per-message status. |
| **Composer** | Title, body, domains (pick prebuilt or type comma-separated, max 3), media, disclosure selector (*Persona / Alias / Anonymous*). |
| **Search** | Full-text results across synced posts and replies. |
| **My posts** | Posts and replies made by any of this account's personas. Local only. |
| **Chat** | Persona DMs: conversation list + conversation view. |
| **Missed** | Two columns: **Outbox** (unsent items, Retry) and **Catch-up missed** (Load button, last catch-up time, results). |
| **Backup & restore** | Show recovery phrase (password required); restore from phrase. |
| **Settings** | Rotation policy (*Keep / Manual / Auto*), "Rotate now" (Manual), default disclosure, alias name, store-node override, private-forum management. |
| **Account switcher** | List accounts, create, switch, rename. |

### 3.3 Status everywhere

Every message the user sends shows one of:

| Status | Meaning |
|---|---|
| **Sending…** | Accepted locally, travelling to the network. |
| **Live** | Confirmed by the network. |
| **Failed to publish** | Not delivered. **Retry** button; also listed in *Missed → Outbox*. |

A global banner shows *Offline*, *Connecting…* or *Connected* (from `delivery_module`'s `connectionStateChanged`).

---

## 4. Architecture

### 4.1 Modules

Forumer ships as **two Logos modules**:

| Module | Type | Responsibility |
|---|---|---|
| **`forumer_core`** | `core` module, C++ (`interface: universal`) | All logic: identity, crypto, envelopes, storage, sync, reliability, anti-flood, DMs, media. Has no UI and can be used by other apps. |
| **`forumer_ui`** | `ui_qml` module | Basecamp app: QML views + a thin C++ backend that forwards calls to `forumer_core` and exposes state to QML. |

Splitting the engine from the UI is a deliberate supportability choice: the forum protocol becomes a reusable component (another app — a bot, a CLI, a different UI — can depend on `forumer_core`).

> ⚠ verify: a `ui_qml` module depending on a custom `core` module, both packaged and released through the catalog. Fallback: one `ui_qml` module whose backend links `forumer_core` as a static library, with the same internal boundary.

### 4.2 Process model (inherited from Logos Basecamp)

```
┌─ Basecamp process ────────────────────────────────┐
│  forumer_ui QML view (sandboxed: no network,       │
│  no filesystem outside its own dir)                │
│        │ logos.callModuleAsync / QtRO replica      │
└────────┼───────────────────────────────────────────┘
         │ IPC (local socket)
┌────────▼─────────────┐   ┌──────────────────────┐   ┌──────────────────────┐
│ ui-host              │   │ logos_host           │   │ logos_host           │
│ forumer_ui backend   │──▶│ forumer_core         │──▶│ delivery_module      │
└──────────────────────┘   │                      │──▶│ storage_module       │
                           └──────────────────────┘   └──────────────────────┘
```

- Each module runs in its own process; calls cross process boundaries and are token-authenticated by the Logos runtime.
- `delivery_module` and `storage_module` are **shared singletons** across all Basecamp apps. `forumer_core` must tolerate a node that another app already created (`createNode` → "already created" → proceed to `subscribe`).
- Forumer keeps **its own state in its own SQLite database**, never in shared modules.

### 4.3 Inside `forumer_core`

```
forumer_core
├─ identity    master secret, password vault, recovery phrase, persona derivation,
│              rotation policy, accounts
├─ crypto      libsodium wrappers: Ed25519, X25519, XChaCha20-Poly1305, Argon2id,
│              BLAKE2b, KDF
├─ codec       envelope encode/decode, canonical bytes, ids, signature verify
├─ store       SQLite (+ FTS5): accounts, personas, posts, domains, follows,
│              outbox, dm, private forums, hides/mutes, sync cursors
├─ sync        delivery transport, subscribe/publish, store-query catch-up,
│              Storage snapshots, SDS gap repair, dedupe
├─ outbox      persistent queue, state machine, retry
├─ policy      size caps, rate limiter, proof-of-work mint/verify, tag normalization
├─ forums      public channel + private forums (keys, invite links)
├─ dm          persona-to-persona encrypted messages
└─ media       metadata stripping, encryption (private forums), Storage upload/download, cache
```

Reused from the sample app (adapted): the `cloud_data_core` sync/storage library, the `DeliveryModuleTransport` and `StorageModuleBlobStore` adapters, delivery-state tracking. New: identity, crypto, codec, policy, private forums, DMs, media, search, the UI.

### 4.4 Dependencies

| Dependency | Use | Version |
|---|---|---|
| `delivery_module` | Publish/subscribe, store queries, reliable channels (SDS), RLN state | v0.3.0 (sample pinned v0.2.1 — upgrade ⚠ verify) |
| `storage_module` | Media and snapshot blobs by CID | v2.1.x |
| `logos-module-builder` | Nix build, codegen, `.lgx` packaging | as pinned |
| `logos-design-system` | QML components and styling | as pinned |
| libsodium | All cryptography | system/Nix |
| SQLite (with FTS5) | Local store and search | system/Nix |
| nlohmann/json | Envelope JSON | system/Nix |

`keystore_signer` (used by the sample) is **dropped**: it can sign but not verify, and its keys are not derivable from a master secret. `forumer_core` owns its keys.

---

## 5. Identity and privacy model

### 5.1 Accounts and the master secret

- An **account** is one **master secret**: 32 random bytes generated on first run.
- The master secret is **never published, never used to sign anything public**, and never leaves the device except as the recovery phrase.
- Multiple accounts = multiple master secrets, each with its own personas, settings, *My posts* and DMs. The account switcher selects the active one.

### 5.2 Password vault and recovery phrase

| Item | Mechanism |
|---|---|
| At-rest encryption | Password → **Argon2id** (`crypto_pwhash`, moderate limits) → 32-byte key → **XChaCha20-Poly1305** encrypts the master secret. Salt and params stored alongside. |
| Unlock | Required at each app start; the decrypted secret lives only in `forumer_core` memory (`sodium_mlock` where available). |
| Recovery phrase | **BIP-39** 24-word mnemonic encoding the 32-byte master secret. Shown once at onboarding and again from *Backup & restore* (password required). |
| Restore | Enter phrase → choose a new password → personas are re-derived ([§5.5](#55-my-posts-and-restore)). |
| Forgotten password | Restore from phrase. No phrase → identity is unrecoverable (stated at onboarding). |

### 5.3 Rotation policy

Personas are Ed25519 key pairs derived deterministically:

```
persona_seed[i] = crypto_kdf_derive_from_key(32, subkey_id = i, ctx = "frmrpers", key = master)
persona_key[i]  = crypto_sign_seed_keypair(persona_seed[i])
```

The account keeps a counter `i`. The rotation policy (Settings) decides which persona signs the next post:

| Policy | Behaviour |
|---|---|
| **Keep** | Always persona `i`; stable public identity. |
| **Manual** | Persona `i` until the user taps **Rotate now** → `i+1`. |
| **Auto** | A **new persona for every post and every reply**: `i+1` each time. |

Consequence of *Auto*: a post's author replying in their own thread appears as a different persona. That is the intended privacy trade-off; the user can override to *Persona* disclosure for a single message in the composer.

Without the master secret, personas cannot be linked to each other or to the account (KDF outputs are independent).

### 5.4 Disclosure per post

The composer's disclosure selector controls what is shown alongside the signature:

| Disclosure | Signing key | Shown publicly | LP wording |
|---|---|---|---|
| **Persona** | Current persona (per rotation policy) | Persona fingerprint (e.g. `fr:7Q4K-M2XD`) | "unique id" |
| **Alias** | Current persona | Chosen alias name + optional fingerprint toggle | "alias (alongside optional uid)" |
| **Anonymous** | A fresh one-time persona, regardless of policy | Nothing ("Anonymous") | "without revealing any id" |

Every post is signed, so its integrity is always verifiable; only *Persona* and *Alias* make the signer recognisable across posts.

### 5.5 My posts and restore

- *My posts* is a local index: `persona index → message ids`. No one else can compute it.
- **Restore on a new device:** derive personas `0..N` from the phrase and match their public keys against synced messages, extending `N` while matches are found (a **gap limit** of 50 unused personas, as in HD wallets). The account's counter is set past the last match.
- Restore therefore recovers *My posts* for everything the device can catch up on ([§9.2](#92-catch-up-after-being-offline)).

### 5.6 Signature verification

Every received envelope is verified (`crypto_sign_verify_detached`) against the public key it carries before storage or display. Unsigned, malformed or invalid messages are dropped and counted in debug logs.

---

## 6. Data model

### 6.1 Concepts

| Concept | Description |
|---|---|
| **Post** | A new discussion: title, body, 1–3 domain tags, optional media. |
| **Comment** | A reply to a post (`parent = post`). |
| **Reply** | A reply to a comment or another reply (`parent = that message`). The first reply under a comment opens a sub-thread. |
| **Domain** | A normalized tag. Exists once any post uses it. |
| **Follow** | Local: the domains this account follows. |
| **Forum** | `public` (the shared channel) or a private forum (by forum id). |

Every comment/reply carries `root` (the post id) and `parent` (direct parent id), so the thread tree can be rebuilt from any arrival order. Replies whose parent hasn't arrived yet are kept and attached when it does.

### 6.2 SQLite schema (sketch)

```sql
accounts      (id, label, vault_blob, created_at, persona_counter, rotation_policy,
               default_disclosure, alias_name)
personas      (account_id, idx, pubkey, created_at)          -- local only
messages      (id PK, kind, forum_id, root_id, parent_id, title, body,
               author_pubkey, alias, show_fp, ts, pow_bits, raw_envelope,
               received_at, verified)
message_domains (message_id, domain)
media         (message_id, cid, mime, size, sha256, enc_key NULL, cached_path NULL)
my_messages   (account_id, persona_idx, message_id)          -- "My posts"
follows       (account_id, domain)
outbox        (id PK, account_id, envelope, topic, state, attempts, last_error,
               request_id, created_at, updated_at)
forums        (id PK, name, key NULL, joined_at)              -- private forums
dm_messages   (id, account_id, peer_pubkey, my_persona_idx, direction, body, ts, state)
hides         (account_id, target_type, target_id)            -- post/thread/persona
sync_cursors  (topic, last_ts, last_cursor)
fts_messages  USING fts5(title, body, domains, content='messages')
```

### 6.3 Domain tags

- Composer offers prebuilt domains (`privacy`, `politics`, `tech`, `campus`, `science`, `culture`, `meta`) and a free-text field accepting comma-separated tags.
- **Normalization:** trim → lowercase → collapse internal whitespace to `-` → allow `[a-z0-9-]`, 2–32 chars → dedupe. Max **3 per post**.
- *All domains* lists every tag seen, sorted by recent activity; one-off junk tags sink.

---

## 7. Wire format

### 7.1 Envelope (v1)

Posts, comments and replies are **immutable, signed envelopes**, JSON-encoded (UTF-8):

```json
{
  "v": 1,
  "kind": "post",                       // post | reply | snapshot | dm (see §12)
  "forum": "public",                    // or private forum id (hex)
  "root": null,                         // post id for replies
  "parent": null,                       // direct parent id for replies
  "title": "Is SIM-Aadhaar linking a privacy risk?",
  "body": "…",
  "domains": ["privacy", "politics"],
  "media": [{ "cid": "…", "mime": "image/jpeg", "size": 183244, "sha256": "…" }],
  "alias": null,                        // alias disclosure only
  "show_fp": true,
  "pk": "<ed25519 pubkey, base64url>",
  "ts": 1790000000000,                  // ms since epoch, author-claimed
  "pow": { "nonce": "<base64url>", "bits": 18 },
  "sig": "<ed25519 signature, base64url>"
}
```

### 7.2 Canonical bytes, id, signature, PoW

1. **Canonical bytes** = the envelope with `sig` removed and `pow.nonce` set to `""`, serialized with sorted keys and no whitespace.
2. **Proof-of-work:** find `nonce` such that `BLAKE2b-256(canonical_bytes || nonce)` has at least `pow.bits` leading zero bits.
3. **Signature:** `sig = Ed25519(persona_sk, canonical_bytes || nonce)`.
4. **Message id** = `hex(BLAKE2b-256(canonical_bytes || nonce || sig))[0..32]`. Content-addressed: duplicates and retries are idempotent.

### 7.3 Validation on receipt (in order)

size within limits → JSON shape and field limits → PoW meets the required bits for its kind → signature valid → not hidden/muted → store → index → notify UI.

### 7.4 Private forum framing

Inside a private forum the envelope above is the **plaintext**; the network carries:

```json
{ "v": 1, "kind": "sealed", "forum": "<forum id>", "n": "<24-byte nonce>", "ct": "<XChaCha20-Poly1305 ciphertext>" }
```

### 7.5 Versioning

`v` is checked first; unknown versions are ignored (not errors), so newer clients can coexist with older ones.

---

## 8. Network layout (content topics)

All topics follow [LIP-23](https://lip.logos.co/messaging/informational/23/topics.html): `/{app}/{version}/{name}/{encoding}`.

| Topic | Carries | Why |
|---|---|---|
| `/forumer/1/public/json` | All public posts, comments, replies | One shared stream: no per-domain duplication, and the network cannot see which domains a user reads. Domains are filtered locally. |
| `/forumer/1/pf-{H(forum_key)[0..8]}/json` | One private forum's sealed envelopes | Only key holders can derive the topic and decrypt. |
| `/forumer/1/dm-{bucket}/json` | Persona DMs | 256 buckets by `H(recipient_pk)[0]`; recipients trial-decrypt their bucket. |
| `/forumer/1/snap/json` | Snapshot pointers (CID + range) | Lets late joiners fetch history from Storage. |

**Scaling note:** if public traffic grows beyond what every client should receive, the public topic can be split into N buckets by `H(post id)` with clients subscribing to all buckets — no protocol change for envelopes.

**Network preset:** `{ "mode": "Core", "preset": "logos.test" }` (layered config shape; no bare `WakuNodeConf` keys at the top level — see the sample's delivery notes).

---

## 9. Reliability: outbox, catch-up, gap repair

### 9.1 Outbox

Every outgoing message is written to the `outbox` table **before** any network call.

```
 queued ──send()──▶ sending ──messageSent──▶ live
                       │
                       └──messageError / timeout / offline──▶ failed ──Retry──▶ sending
```

| UI label | States |
|---|---|
| Sending… | `queued`, `sending` |
| Live | `live` |
| Failed to publish (+ Retry) | `failed` |

- Survives restarts; on start, `sending` items older than the timeout become `failed`.
- **Retry** re-sends the identical envelope (same id), so retries never create duplicates.
- *Missed → Outbox* lists every non-live item with its error and a Retry button. Composer text is never lost.

### 9.2 Catch-up after being offline

Runs **automatically once on startup** (after unlock and connection) and **manually** via *Missed → Catch-up missed → Load*.

1. **Store nodes:** for each subscribed topic, `delivery_module.storeQuery` with `timeStart = sync_cursors.last_ts − 5 min` (overlap for clock skew), paginated via `paginationCursor`, bounded timeout.
   - Store peer address: from config/preset default, overridable in Settings. ⚠ verify which `logos.test` peers serve store queries.
2. **Storage snapshots:** if the gap is older than store retention, or store queries fail, fetch the newest snapshot pointers from `/forumer/1/snap/json` history and download the referenced blobs from Logos Storage.
3. All results go through the normal validation path ([§7.3](#73-validation-on-receipt-in-order)); dedupe by id.
4. The Missed tab shows: last catch-up time, messages recovered, and source.

**Snapshots** are produced by clients opportunistically: every N new messages (default 200) on a topic, a client may bundle recent envelopes into a blob, upload it to Storage, and publish a pointer. Snapshots contain the original signed envelopes, so they need no trust in the uploader.

### 9.3 Automatic gap repair (SDS)

While the app is open and connected, gaps are detected and repaired without user action using **Scalable Data Sync** via `delivery_module`'s reliable channels (`channelCreate` / `channelSend` / `channelMessageReceived`). Recovered replies attach to their threads silently.

> ⚠ verify: SDS reliable channels suit a many-writer public topic. Fallback: detect gaps from orphaned `parent` references and periodic short-range store queries, fetching missing ids via `storeQuery` `messageHashes`.

---

## 10. Anti-flood and spam resistance

Personas are free, so spam must cost something other than identity.

| Measure | Default (tunable after measurement) |
|---|---|
| Title / body size | 200 chars / 10,000 chars |
| Domains per post | 3 |
| Media per post | 4 files, 5 MB each |
| Envelope size | 16 KB (media is by reference) |
| Client send rate limit | 5 messages per minute per account, burst 3 |
| PoW — persona/alias posts and replies | 16 bits |
| PoW — anonymous messages | 20 bits |
| PoW — DMs | 16 bits |
| Network RLN | Used automatically when the network preset enables it (`rlnState`) |
| Relay behaviour | Clients never re-publish received messages; history is fetched on demand, never rebroadcast |

PoW targets **a few seconds on a low-end laptop**; values are set from benchmarks and reported in the FURPS *Performance* section. Messages below the required bits are dropped on receipt.

---

## 11. Private forums

- **Create:** generate a random 32-byte `forum_key`. `forum_id = hex(BLAKE2b(forum_key))[0..16]`.
- **Invite link:** `forumer://join/{base58(forum_key)}?name={url-encoded name}`. Paste it in the app (or click it where supported) to join.
- **Content:** every envelope is encrypted with XChaCha20-Poly1305 under a key derived from `forum_key`; the topic is derived from `forum_key`. Relays, store nodes and non-members see only ciphertext and an opaque topic.
- **Media** in private forums is encrypted before upload; the per-file key travels inside the encrypted envelope.
- **Membership is the link.** Anyone with it can read and post. Sharing it publicly makes the forum readable by anyone who sees it (but still opaque to the network). This is stated in the UI.
- **Key rotation (later):** the creator issues a new link; members without the new link lose access to new content.

Private forums appear as their own section on Home alongside the public domains.

---

## 12. Persona DMs

Logos `chat_module` is **not used** for DMs: it exposes one address per installation (and a new address per session), so DMs from different personas would be linkable to one sender. Forumer implements persona-addressed DMs over Delivery:

1. Click a persona (on a post or reply) → **Message**.
2. Sender converts the recipient's Ed25519 key to X25519 (`crypto_sign_ed25519_pk_to_curve25519`).
3. Payload (sender's persona pubkey, body, ts, PoW) is encrypted with a **sealed box** (ephemeral X25519, `crypto_box_seal`), so the network sees neither sender nor recipient.
4. Published to `/forumer/1/dm-{H(recipient_pk)[0]}/json`.
5. Every client subscribes to the buckets of its own personas and trial-decrypts; failures are discarded silently.
6. The recipient replies to the sender's persona the same way.

The sender always messages from one of their personas (by default, the persona that wrote the post or reply they are responding to, or a fresh one). Neither side learns the other's master identity.

**Limits (stated in UI and threat model):** no forward secrecy (a stolen persona key decrypts past DMs to it), metadata at the network layer still exists ([§15](#15-threat-model)). A ratchet is future work.

Group chats are out of scope for this version.

---

## 13. Media

1. User attaches an image or file in the composer.
2. **Metadata stripping:** images are decoded and re-encoded (drops EXIF/GPS/device data). Allowed types: JPEG, PNG, WebP, GIF (static frame), PDF, plain text. Others rejected with a clear message.
3. Private forums: encrypt with a random per-file key.
4. Upload via `storage_module` (`uploadInit` → `uploadChunk` → `uploadFinalize`) → **CID**.
5. CID, MIME, size and SHA-256 go into the envelope.
6. Viewers download by CID (`downloadChunks`), verify SHA-256, decrypt if needed, cache locally.
7. If unavailable: placeholder with **Retry**; never blocks reading the text.

> ⚠ verify: Logos Storage availability when the uploader is offline, and whether other clients should re-host fetched media.

---

## 14. Search, hide and mute

- **Search:** SQLite **FTS5** over title, body and domains of everything stored locally (including caught-up history). Results link into threads. Private forum content is searchable only on members' devices (it is stored decrypted locally).
- **Hide:** hide a post or a whole thread from your own view.
- **Mute persona:** hide everything from a persona. (Less effective against Auto-rotating posters — hence per-post/thread hide.)
- All moderation is **local**: Forumer has no global moderators, consistent with its no-central-authority design.

---

## 15. Threat model

### 15.1 Protects against

| Threat | Protection |
|---|---|
| Linking a user's posts together | Persona rotation (Keep/Manual/Auto); KDF-independent persona keys |
| Linking personas to a real identity via the app | No accounts, emails, phone numbers or servers; master secret never published |
| Forged or altered posts | Ed25519 signatures on every envelope, verified on receipt |
| Censorship by a single operator | No central server; Logos Delivery peer-to-peer relay; history in Storage |
| Reading private forums without the link | Encryption + key-derived topics |
| Reading DMs in transit | Sealed boxes; recipients not identifiable from the topic beyond a 1/256 bucket |
| Location leaks via photos | Metadata stripped before upload |
| Device theft (powered off) | Master secret encrypted with Argon2id-derived key |
| Cheap spam | PoW, rate limits, size caps, RLN |

### 15.2 Does **not** protect against

| Limitation | Notes |
|---|---|
| Network-level metadata | All personas of an account publish from the same node; a powerful observer of the user's network or of relay peers can correlate timing and IP. Use a VPN/Tor-style transport if this matters (outside Forumer's scope). |
| Writing-style analysis | Rotation does not change how someone writes. |
| A compromised, unlocked device | Malware with access while unlocked can read the secret. |
| Leaked invite links | Anyone with a private forum's link can read it. |
| Forward secrecy for DMs | Not provided in this version. |
| Clock honesty | `ts` is author-claimed; ordering is best-effort. |
| Permanent deletion | Peer-to-peer data cannot be reliably deleted once published; Forumer does not offer delete. |

---

## 16. Repository structure

```
forumer/
├── README.md                      ← this document
├── CHANGELOG.md                   ← changes vs. the sample, with reasons
├── LICENSE-MIT
├── LICENSE-APACHE-v2
├── flake.nix / flake.lock         ← top-level dev shell + builds both modules
├── docs/
│   ├── protocol.md                ← envelope, topics, crypto (normative)
│   ├── threat-model.md
│   ├── adr/                       ← architecture decision records (one per decision)
│   └── demo-script.md             ← video walkthrough checklist
├── modules/
│   ├── forumer_core/              ← core module (reusable engine)
│   │   ├── metadata.json
│   │   ├── flake.nix
│   │   ├── CMakeLists.txt
│   │   ├── include/forumer/       ← public C++ API
│   │   ├── src/
│   │   │   ├── identity/          ← vault, mnemonic, personas, accounts
│   │   │   ├── crypto/            ← libsodium wrappers
│   │   │   ├── codec/             ← envelope, canonical bytes, verify
│   │   │   ├── store/             ← SQLite schema, migrations, FTS
│   │   │   ├── sync/              ← transport, catch-up, snapshots, SDS
│   │   │   ├── outbox/
│   │   │   ├── policy/            ← limits, rate limit, PoW, tags
│   │   │   ├── forums/            ← public + private forums
│   │   │   ├── dm/
│   │   │   ├── media/
│   │   │   └── forumer_core_module.{h,cpp}   ← Logos module glue
│   │   ├── third_party/cloud_data_core/       ← adapted from sample
│   │   └── tests/
│   └── forumer_ui/                ← ui_qml Basecamp app
│       ├── metadata.json
│       ├── flake.nix
│       ├── CMakeLists.txt
│       ├── src/
│       │   ├── forumer_ui.rep     ← QtRO contract (slots, props, signals)
│       │   ├── forumer_ui_backend.{h,cpp}
│       │   └── qml/
│       │       ├── Main.qml
│       │       ├── onboarding/
│       │       ├── home/
│       │       ├── thread/
│       │       ├── composer/
│       │       ├── chat/
│       │       ├── missed/
│       │       ├── profile/
│       │       └── settings/
│       └── assets/
├── scripts/
│   ├── two-instances.sh           ← local end-to-end test
│   └── bench-pow.sh
└── .github/workflows/ci.yml
```

> ⚠ verify: whether the module catalog accepts two modules from one repository (`add-module.sh` adds one submodule per module repo). Fallback: split into `forumer-core` and `forumer-ui` repositories.

---

## 17. Build, run and install

### 17.1 Prerequisites

- **Nix** with flakes enabled (`experimental-features = nix-command flakes`)
- macOS (Apple Silicon) or Linux (x86_64/arm64)
- Access to the public Logos Nix cache (`cache.nix.logos.co`, no credentials)

### 17.2 Build

```bash
git clone https://github.com/bristinWild/forumer
cd forumer
nix build .#forumer_core -L
nix build .#forumer_ui   -L
```

### 17.3 Run (development)

```bash
# Standalone single-app harness (fast iteration; not proof of portability)
nix run .#forumer_ui

# Two isolated instances to test end to end
./scripts/two-instances.sh
```

### 17.4 Run in Basecamp (real distribution path)

```bash
nix build .#forumer_ui-lgx-portable    # self-contained .lgx, no /nix/store refs
```

Then in Basecamp: *Package Manager → Install from file* → select the `.lgx`, or install from the catalog below. Two Basecamp instances can run side by side with `--user-dir /tmp/bc-a` and `--user-dir /tmp/bc-b`.

### 17.5 Module catalog

Forumer is published from a fork of `logos-modules-release-base`. Add this repository in Basecamp's package manager:

```
https://raw.githubusercontent.com/bristinWild/forumer-modules/main/logos-repo.json
```

Variants built: `darwin-arm64`, `linux-amd64`, `linux-arm64` (+ `windows-x86_64` if the Windows cross-build is supported ⚠ verify).

### 17.6 Using Forumer (step by step)

1. Open Forumer in Basecamp → **Create identity** → set a password → **save the recovery phrase**.
2. **Home → All domains** → **Follow** a few domains.
3. **New post** → title, body, pick or type domains → choose disclosure → **Post**. Watch *Sending… → Live*.
4. Open a post → **Comment**; open a comment → **Reply** to start a sub-thread.
5. **Settings → Identity rotation** → try *Auto*; post twice and compare the two personas.
6. Go offline, post → it shows **Failed to publish** → **Missed → Outbox → Retry** after reconnecting.
7. Close the app; post from another instance; reopen → catch-up runs automatically (**Missed → Catch-up missed** shows what was recovered).
8. **Private forum:** *Create private forum* → copy invite link → join from the second instance.
9. Click a persona → **Message** → DM from the other instance.
10. **Backup & restore** → restore on the second instance from the phrase → **My posts** reappears.

---

## 18. Testing and CI

| Layer | What | How |
|---|---|---|
| Unit | codec (canonical bytes, ids), signatures, PoW, KDF/persona derivation, vault, mnemonic round-trip, tag normalization, outbox state machine, thread tree building, sealed DM round-trip | C++ tests in `modules/forumer_core/tests`, run by `nix flake check` |
| Integration | Two instances on `logos.test`: post → receive; reply tree; offline → catch-up; outbox retry; private forum; DM | `scripts/two-instances.sh` (manual / self-hosted runner — needs live network) |
| UI | Manual checklist in `docs/demo-script.md`; non-expert walkthrough with real users before submission | — |
| Clean-environment check | Fresh machine/VM: clone → build → run without modification | Before every submission |

**CI (`.github/workflows/ci.yml`):** Nix build of both modules, unit tests, `.lgx` build. Must be green on `main`.

---

## 19. Roadmap

### P0 — LP requirements
- [ ] Fork builds and runs; bump `delivery_module` to v0.3.0
- [ ] `forumer_core` / `forumer_ui` split
- [ ] Master identity, password vault, recovery phrase, accounts + switcher
- [ ] Personas, rotation (Keep / Manual / Auto-per-post), disclosure (Persona / Alias / Anonymous)
- [ ] Envelope v1: signing, verification, PoW, ids
- [ ] Public channel; domains as tags; posts, comments, nested replies
- [ ] Home (All / Followed domains), domain view, thread view, composer
- [ ] Outbox + statuses (Sending / Live / Failed + Retry)
- [ ] Catch-up (auto on start + Load): store queries + Storage snapshots
- [ ] Gap repair (SDS or fallback)
- [ ] Anti-flood limits
- [ ] Catalog release, CI green, README, protocol + threat-model docs

### P1 — Differentiators
- [ ] Search (FTS5)
- [ ] Media with metadata stripping
- [ ] My posts + restore with gap-limit scan
- [ ] Hide / mute
- [ ] Private forums + invite links
- [ ] Missed tab polish, offline banner, error copy

### P2 — Advanced
- [ ] Persona DMs

### Submission
- [ ] Adoption evidence collected ([§22](#22-adoption-plan))
- [ ] Narrated demo video (setup → all flows → failure handling → design choices)
- [ ] FURPS self-assessment + solution PR (`solutions/LP-0026.md`)

---

## 20. Out of scope and future work

| Item | Reason |
|---|---|
| **Donations / tipping** | Requires the blockchain module, explicitly out of scope for LP-0026; also, payouts to personas can be linked when funds are consolidated. Candidate for a follow-up LP using LEZ private balances. |
| **Group chats** | Deferred; DMs first. |
| **Edit / delete posts** | Cannot be enforced peer-to-peer; offering it would mislead users. |
| **DM forward secrecy** | Needs a ratchet; future work. |
| **Membership proofs (ZK/RLN-based anonymous credentials)** | Would allow "verified member, unknown who" — natural follow-up with LEZ. |
| **Mobile** | Basecamp is desktop-first. |

---

## 21. Open questions and risks

| # | Question / risk | Plan |
|---|---|---|
| 1 | Which `logos.test` peers answer store queries; retention window | Test week 1; fall back to Storage snapshots |
| 2 | Logos Storage availability when uploader is offline | Test; consider re-hosting fetched blobs |
| 3 | `ui_qml` → custom `core` module dependency, and two modules in one catalog repo | Test week 1; fallbacks in [§4.1](#41-modules), [§16](#16-repository-structure) |
| 4 | `delivery_module` v0.2.1 → v0.3.0 API changes | Upgrade first, before new features |
| 5 | SDS suitability for a many-writer public topic | Prototype; fallback in [§9.3](#93-automatic-gap-repair-sds) |
| 6 | Basecamp on Windows (seminar audience) | Check; prepare pre-installed fallback laptops |
| 7 | PoW cost on low-end hardware | Benchmark; tune bits |
| 8 | First-come-first-served competition | Prioritise P0, submit once P0+P1 and adoption are solid |

---

## 22. Adoption plan

LP-0026 requires realistic topics and organic conversations, with authenticity checks.

- **Seminars:** privacy-technology talks at universities; students install Forumer and discuss real questions seeded during the talk (surveillance, digital ID, campus free speech, ZK basics).
- **Sustained use:** a private forum per class/club; prompts over following weeks; multiple institutions; Logos Circles and Discord *#builder-hub*.
- **Onboarding kit:** one-page install guide; pre-tested venue network; fallback pre-installed laptops.
- **Consent:** participants are told it is experimental software and what it does and does not protect ([§15](#15-threat-model)).
- **Evidence (without deanonymizing anyone):** distinct personas over time, threads with multi-party replies, activity spread across days, domain diversity, seminar dates/venues. Note that Logos CCs do not count toward adoption.

---

## 23. Credits and license

- Forked from and inspired by [jzaki/forum-sample-app](https://github.com/jzaki/forum-sample-app) (MIT / Apache-2.0). Reuse confirmed acceptable by the LP author. Changes are documented in `CHANGELOG.md`.
- Built on the [Logos](https://logos.co) stack.

Dual-licensed under the **MIT License** and the **Apache License 2.0**, at your option. See `LICENSE-MIT` and `LICENSE-APACHE-v2`.

> This is an independent community project. It is not built for, on behalf of, or endorsed by Logos or the Institute of Free Technology.