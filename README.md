# Forumer

**A private, serverless discussion forum for Logos Basecamp.**

Forumer lets people discuss any topic (politics, privacy, campus life, technology) without a central server, without an account tied to their real identity, and without one profile that links everything they say. Posts travel peer to peer over **Logos Delivery**; everything else (identities, the post log, unsent posts) stays on the user's own device.

Built for [λPrize LP-0026: Forum App](https://github.com/logos-co/lambda-prize/blob/master/prizes/LP-0026.md). Forked from [jzaki/forum-sample-app](https://github.com/jzaki/forum-sample-app).

> **Demo video:** [watch the end-to-end walkthrough](https://youtu.be/Tc-MUpDcn7M) (about 14 min: install from the catalog, accounts, topics and replies, personas, links, offline, history, flood control).
>
> **Install in Basecamp:** Settings → Package Repositories → add `https://raw.githubusercontent.com/bristinWild/forumer-catalog/main/logos-repo.json`, then Package Manager → Social → Forumer → Install.

> **Status:** working and in active development. This README describes what is built today. Planned features are marked **(planned)**. The original design document is kept in [`docs/Forumer-description.md`](docs/Forumer-description.md).

---

## Contents

1. [Features](#1-features)
2. [Quick start](#2-quick-start)
3. [Using Forumer](#3-using-forumer)
4. [How it works](#4-how-it-works)
5. [Identity and privacy](#5-identity-and-privacy)
6. [Posts and threads](#6-posts-and-threads)
7. [Network and sync](#7-network-and-sync)
8. [Flood control](#8-flood-control)
9. [Threat model](#9-threat-model)
10. [Design decisions and findings](#10-design-decisions-and-findings)
11. [Repository layout](#11-repository-layout)
12. [Testing](#12-testing)
13. [LP-0026 requirements](#13-lp-0026-requirements)
14. [Roadmap](#14-roadmap)
15. [Credits and license](#15-credits-and-license)

---

## 1. Features

| Feature | What it does |
|---|---|
| **Accounts** | Several accounts per device, each with its own password. An account is one secret master key that is never published. |
| **Recovery phrase** | 24 words (BIP-39 encoding) bring an account back on any device. |
| **Personas** | Public identities derived from the master key. Nobody can tell two personas belong to the same account. |
| **Identity rotation** | *Keep* one persona, rotate *Manually*, or *Auto*: a new persona for every post. |
| **Disclosure per post** | Post as your persona (`fr:7Q4K-M2XD`), under an alias, or fully anonymously. |
| **Topics and replies** | Topics tagged with up to 3 domains. Replies nest two levels deep. |
| **Domains** | Follow the domains you care about; the home page shows *All* and *Followed* side by side. Starter domains (`#logosdevs`, `#lambdabuilders`, `#testnet-help`, `#privacy`, `#tech`) are offered even before anyone has posted in them, and a new topic started from a domain's page is posted there. |
| **Search** | ⌘K / Ctrl+K searches every post on the device. |
| **Shareable links** | *copy link* on a thread or a #domain gives a `basecamp://` link. Clicked in a browser or chat app, Basecamp 0.3.1+ opens it in Forumer; pasted into Forumer's search box it works on any version. The link names the post, never who shared it, and a reader who doesn't hold the thread yet asks peers for that whole week of posts, so nobody learns which thread was opened. See [Sharing links](#sharing-links). |
| **Post status** | Every post you write shows *sending… → live*, or *failed* with retry. |
| **Outbox** | Posts that couldn't be sent are kept on disk and retried automatically, even after a restart. |
| **Offline catch-up** | Posts written while you were offline arrive when you come back. |
| **Full history** | A newcomer gets the whole forum, not just the last two days. On joining, Forumer asks the network's own **store nodes** for what they kept, so this works even when nobody else is online; older posts are fetched from peers a week at a time, back to the oldest post anyone holds. |
| **Formatting** | Topics and replies are written in Markdown with a toolbar (headings, bold, italic, code, lists, quotes, links), ⌘B / ⌘I / ⌘E, and a Write / Preview switch. Images and HTML in posts are never loaded, so reading a post can't reveal your IP address to its author. |
| **Replies to you** | A badge and a list of replies to your posts and new replies in threads you started or joined, plus a notice when one arrives. Worked out on your device only — it even works for posts you made anonymously, and nobody can tell who was notified. |
| **Missed tab** | Your unsent posts plus a *Catch up* button. |
| **My posts** | Everything this account wrote, under any persona. Only you can see this list. |
| **Flood control** | Proof-of-work on every post, hourly limits per persona and per account, with a counter in the composer. |
| **Backup & restore** | Show the recovery phrase (password required), or restore an account from one. |
| **Night-city theme** | Dark and light palettes with Chakra Petch and Share Tech Mono; delivery states colour-coded (sending, live, failed). |
| Media and history snapshots via Logos Storage | **(investigated, planned)** — see [§10](#10-design-decisions-and-findings) |
| Private messages between personas | **(planned)** |
| Mute a persona / hide a post | **(planned)** |

---

## 2. Quick start

### Prerequisites

- **Nix** with flakes enabled (`experimental-features = nix-command flakes`)
- macOS (Apple Silicon) or Linux (x86_64 / arm64)

### Build and run

```bash
git clone https://github.com/bristinWild/forumer
cd forumer
nix build -L       # build the module
nix run            # run Forumer in a standalone Basecamp window
```

The first build downloads the Logos modules (delivery, storage) and can take a while.

Versions: logos-module-builder **0.3.2**, delivery_module **v0.3.0**, storage_module **v3.0.0** — the 0.3 module generation that current Basecamp runs (0.2 modules don't load in 0.3).

On macOS the first launch asks whether to accept incoming network connections: choose **Allow** (the delivery and storage nodes listen for peers).

### Two instances on one machine (end-to-end test)

```bash
./scripts/two-instances.sh
```

This starts two independent instances, each with its own data folder (`~/forumer-test/a` and `~/forumer-test/b`, set through `LOGOS_USER_DIR`). They have separate accounts, post logs and network identities, so they behave like two different people. Set `FORUMER_TEST_DIR` to use another folder.

> Never run two instances on the **same** data folder: they would share one post log and one network identity.

To start again from nothing: `rm -rf ~/forumer-test`.

### Unit tests

```bash
./scripts/test-core.sh
```

This builds `forumer_core` and runs its 110 unit tests. It needs only Nix; cmake, the compiler, libsodium, SQLite and nlohmann/json come from nixpkgs. Set `FORUMER_NO_NIX=1` to use the system's own instead.

### Install in Basecamp (no build needed)

Forumer is served pre-built from its own Logos module catalog, [`bristinWild/forumer-catalog`](https://github.com/bristinWild/forumer-catalog), made from the official [`logos-modules-release-base`](https://github.com/logos-co/logos-modules-release-base):

1. In Basecamp: **Settings → Package Repositories → Add a repository**, and paste
   ```
   https://raw.githubusercontent.com/bristinWild/forumer-catalog/main/logos-repo.json
   ```
2. **Package Manager → Social → Forumer → Install.** Its dependencies, `delivery_module` (~0.3.0) and `storage_module` (~3.0.0), come from the official Logos catalog and are installed with it.
3. Open **Forumer** from the sidebar.

Packages are built for macOS (Apple silicon) and Linux (x86_64).

### Build the package yourself

```bash
nix build .#lgx-portable
```

This produces a self-contained `.lgx` package for Basecamp's package manager.

---

## 3. Using Forumer

1. **Create an account.** Choose a name and a password, then **write down the 24-word recovery phrase**. Without the password *and* the phrase, the account cannot be recovered.
2. **Home** shows two columns: *All domains* and *Followed domains*. Pick a domain chip and press **+ follow #domain**.
3. **New topic** (Ctrl+N): title, optional message, up to 3 domains (pick or type, comma-separated), and how to post: *persona*, *alias* or *anonymous*. The counter under the form shows how many topics you have left this hour.
4. Open a topic and **reply**. Press *reply* under a reply to answer it; the answer opens a sub-thread under that reply.
5. Watch the tag next to your post: **sending…** turns **live** once the network confirms it.
6. **Profile & personas:** see your current persona, rotate it, and choose the rotation policy (*keep / manual / auto*), the default way to post, and your alias.
7. **Offline:** post while disconnected and the post waits in **Missed → outbox**; it goes out automatically when you reconnect (or press *retry*). Close the app, post from the other instance, reopen: the missed posts arrive within seconds.
8. **My posts** lists everything this account wrote, whichever persona it used.
9. **Backup & restore:** show your phrase, or restore an account on another instance.
10. **Lock** (bottom of the sidebar) locks the account again.

### Sharing links

Open a thread and press **copy link**, or pick a domain chip on Home (for example `#lambdabuilders`) and press **copy link** next to *follow*. You get a link like

```
basecamp://intent/forumer.open?p=eyJkb21haW4iOiJsYW1iZGFidWlsZGVycyJ9
```

Post it anywhere: Discord, a chat, a web page.

- **Clicking it** (Basecamp 0.3.1 or newer) opens Basecamp, which hands it to Forumer through the `forumer.open` intent that `metadata.json` declares. A thread link opens the thread; a domain link opens Home filtered on that domain, with *follow* one click away. A link opened while the account is locked opens after unlocking.
- **Pasting it** into Forumer's search box (⌘K) does the same on any Basecamp version. A bare 32-character topic id works too.

What a link carries, and why it is safe to share:

| | |
|---|---|
| Thread link | `{"topic": "<post id>", "day": <days since 1970>}`, base64url |
| Domain link | `{"domain": "<name>"}` |
| Who shared it | Not in the link. A post id is a hash of the post itself. |
| Who opened it | No server is involved: the link only tells your own Basecamp what to show. |
| What the network learns | If you don't hold the thread yet, Forumer asks peers for every post from that day and the week after (the ordinary history range request), not for that one post. |
| Untrusted input | Anyone can write a link. Basecamp checks the payload against the declared parameters, and Forumer checks it again (`src/qml/links.js`): only a 32-hex id, a day number and a domain name are accepted. |

---

## 4. How it works

### Components

```
┌─ Basecamp ───────────────────────────────────────────┐
│  QML view (src/qml)                                   │
│    ForumStore.qml - the view's data layer             │
│        │  Qt Remote Objects (src/forumer.rep)         │
└────────┼──────────────────────────────────────────────┘
┌────────▼──────────────────────┐      ┌──────────────────┐
│ ui-host: ForumerBackend (C++) │─────▶│ delivery_module  │──▶ Logos network
│   links forumer_core          │      └──────────────────┘
└───────────────────────────────┘
```

| Part | Role |
|---|---|
| **`lib/forumer_core`** | The forum engine as a plain C++20 library with no Qt and no UI: identity, vault, recovery phrase, signed posts, proof-of-work, post log, sync rules, thread rules, flood control. It is reusable by any other app (a bot, a CLI, another UI) and fully unit-tested. |
| **`src/forumer_backend.*`** | The Basecamp backend. It wires `forumer_core` to `delivery_module`, runs the timers (digests, retries, quota) and exposes state to the view through the `.rep` contract. |
| **`src/qml`** | The interface. `ForumStore.qml` turns the backend's properties and signals into what the screens show. |

Forumer is one `ui_qml` module. The engine is a separate library inside it, not a separate Logos module, so it builds and ships as one package and can still be reused.

### Data on disk

Everything lives under the instance's data folder: `$LOGOS_USER_DIR/module_data/forumer/` when Basecamp sets one, otherwise the system's app-data folder.

```
forumer/                       (0700; every file below is 0600)
├── accounts/
│   ├── accounts.json          accounts, their names, which one is selected
│   └── <account id>/
│       ├── vault.json         master key, encrypted with the password
│       ├── state.enc          rotation policy, persona counter, alias, followed domains (encrypted)
│       └── own.enc            which posts this account wrote, their send state, replies read (encrypted)
└── posts/
    └── posts.sqlite3          every verified post: public data only
```

`state.enc` and `own.enc` are encrypted (XChaCha20-Poly1305) under a key derived from the account's master key, bound to the account and to the file's purpose. While an account is locked, nothing on disk says which posts it wrote, links its personas, or reveals its alias; only its name (shown on the lock screen) is readable. The unlocked account's list of its own posts lives in memory only.

Upgrading from 0.2.2, which kept the account state and the outbox in clear: each account's data moves into its encrypted files the first time it is unlocked, and the clear tables are dropped from the post log once every account has moved.

---

## 5. Identity and privacy

### Account and master key

An account is 32 random bytes, the **master key**. It never leaves the device except as the recovery phrase, and it never signs anything public.

| Item | How |
|---|---|
| At rest | password → **Argon2id** (3 passes, 256 MiB) → key → **XChaCha20-Poly1305** encrypts the master key. Salt, nonce and cost settings are stored with the vault. |
| Unlock | Needed on every start. The master key is kept in memory only while unlocked. |
| Recovery phrase | The 32 bytes as **24 BIP-39 words** with a checksum, so a mistyped word is caught. It is not a crypto-wallet phrase. Words can be shortened to their first 4 letters. |
| Change password | Re-encrypts the same master key; personas don't change. |

### Personas

```
persona_seed[i] = KDF(master key, id = i, context = "frmrpers")     (libsodium crypto_kdf)
persona_key[i]  = Ed25519 key pair from persona_seed[i]
fingerprint     = "fr:" + first 40 bits of BLAKE2b(public key), Crockford base32   e.g. fr:7Q4K-M2XD
```

Personas are derived, so the recovery phrase brings all of them back, and **My posts** is rebuilt on a new device as the account's posts arrive from the network. Without the master key, two personas cannot be linked.

Anonymous posts are signed with one-time keys derived from a random 64-bit id under their own context (`frmranon`), never from the persona counter: a restore can never hand one out again, and they are not recovered into **My posts**.

**Restoring on a new device.** The new device doesn't hold the account's history yet, so it can't know which personas were already used. Until history has synced from at least one peer, posting waits (the composer says why, with a *post anyway* option); as posts by the account's personas arrive, it resumes as the newest one, never one the user had rotated away from. Persona recovery scans up to 1,000 unused indices ahead.

### Rotation policy

| Policy | Which persona signs the next post |
|---|---|
| **Keep** | Always the same one: a stable public identity. |
| **Manual** | The same one until you press *rotate*. |
| **Auto** | A new persona for every post and reply. |

### Disclosure per post

| Choice | Signed by | Readers see | LP-0026 wording |
|---|---|---|---|
| **Persona** | the policy's persona | `fr:7Q4K-M2XD` | "unique id" |
| **Alias** | the policy's persona | `night owl · fr:7Q4K-M2XD` | "alias (alongside optional uid)" |
| **Anonymous** | a one-time key, never reused | `Anonymous` | "without revealing any id" |

Every post is signed, so it can't be forged or altered. Only *persona* and *alias* make posts recognisable as coming from the same author.

---

## 6. Posts and threads

### Signed envelope

Every topic and reply is an immutable, self-checking JSON document:

```
canonical = all fields except nonce, sig and id; keys sorted, no whitespace
nonce     : BLAKE2b(canonical || nonce) has at least N leading zero bits   (proof-of-work)
sig       = Ed25519(persona, canonical || nonce)
id        = hex(BLAKE2b-128(canonical || nonce || sig))                    (content address)
```

Fields: `v`, `kind` (post / reply), `forum`, `root`, `parent`, `title`, `body`, `domains`, `media`, `ts`, `pk` (persona public key), `disc` (disclosure), `alias`, `fp` (show fingerprint), `pow` (bits), plus `nonce`, `sig` and `id`.

Anyone can check, without trusting the sender, that a post was written by the key it carries, wasn't changed, paid its proof-of-work and has the id it claims. Re-sending a post sends identical bytes, so duplicates are harmless.

| Limit | Value |
|---|---|
| Title / body | 200 / 10,000 characters |
| Domains per topic | 3, normalised to `[a-z0-9-]`, 2–32 characters |
| Alias | 32 bytes; always shown with its fingerprint; can't contain `fr:`, `·`, or invisible / direction-changing characters, or be `Anonymous` |
| Whole envelope | 16 KB (media will travel by reference, never inline) |
| Proof-of-work | 16 bits (persona/alias), 20 bits (anonymous) |
| Clock | posts dated more than 10 minutes in the future are refused |

### Two-level threads

```
topic
├─ reply              level 1: answers the topic
│  ├─ reply           level 2: the sub-thread of that reply
│  └─ reply           (answering a level-2 reply also lands here)
└─ reply
```

Each reply carries `root` (the topic) and `parent` (what it answers), so anyone holding the parent can check where it belongs. A reply that arrives before its parent is shown at level 1 until the parent turns up.

### The post log

`posts.sqlite3` is **append-only**: a post is stored once under its id, and nothing that arrives later can change or remove it. Strangers can add posts (after verification) but cannot overwrite anyone else's.

---

## 7. Network and sync

### One content topic

```
/forumer/3/public/proto          (LIP-23 format)
```

Every public post goes on one shared topic. Domains are filtered on the device, so the network can't see which domains someone reads.

### Which network

| Network | What it is | Default |
|---|---|---|
| `logos.dev` | Logos devnet, open to anyone | ✅ |
| `logos.test` | Official testnet; Delivery v0.3 runs RLN rate limiting there, so the node only starts with an active RLN membership registered on the LEZ testnet | |

Choose in **Settings → choose network** (takes effect after a restart; saved in the account's data folder as `network`). For testing, `FORUMER_NETWORK=logos.test` overrides it. The two are separate networks: you only see people on the same one. Settings → network shows the one in use.

Two kinds of message, both small JSON objects:

```json
{"v":1, "t":"post",   "env":"<signed envelope>"}
{"v":1, "t":"digest", "since":1790000000000, "have":["<16-hex short id>", "..."]}
```

### Outbox: sending your own posts

1. The post is signed and written to the outbox **before** anything touches the network.
2. It is sent and shows **sending…**.
3. When `delivery_module` confirms it, it turns **live**.
4. If sending fails, it is retried automatically with growing delays (15 s, doubling, up to 5 min). You can also retry by hand from **Missed**. Retries survive restarts.

### Digests: filling gaps without a server

A digest says: *"these are the posts I hold since time X"*. Every peer that holds a post in that window that is **not** listed sends it again. So a post someone missed — because they were offline, had only just joined, or the network dropped it — comes back as long as **anyone** who has it is online. The author doesn't need to be.

| When a digest is sent | |
|---|---|
| After joining | at 15 s and 60 s |
| Periodically | every 3 minutes (± 30 s jitter, so peers don't sync in lock-step) |
| On demand | *Catch up* in the Missed tab |
| Prompted | when a peer's digest lists something we lack (at most every 30 s) |

Digests cover the last **48 hours** and list at most 512 ids (64-bit short ids). One peer answers at most 64 posts per digest, after a random 0.5–2.5 s delay. A post someone else re-sent in the last 30 s is skipped, so a room full of peers doesn't all answer at once.

### History from the network's store nodes

Logos Delivery's fleet nodes keep recent messages (the store protocol). A few seconds after joining, Forumer asks them for the forum topic's messages of the last 30 days (the last day, where a node caps the range at 24 h), 100 per page, node by node until one answers with messages:

| | |
|---|---|
| Query | `storeQuery` (async) with `contentTopics: ["/forumer/3/public/proto"]` and the autosharded `pubsubTopic` (`/waku/2/rs/3/4` on logos.dev) |
| Nodes | the six logos.dev entry nodes (`delivery-0{1,2}.<dc>.logos.dev.status.im`); `FORUMER_STORE_PEERS` (comma-separated multiaddrs) overrides, `FORUMER_NO_STORE=1` skips |
| What is taken | posts only — old digests and range requests in the archive are ignored, so a newcomer never answers requests settled days ago |
| Trust | every post is checked like a live one (signature, proof-of-work, flood limits): a store node can withhold posts but not forge or alter them |

This is what lets a fresh install see the forum when nobody else is online. **Missed → history → network archive** shows what came from where.

### History: everything older

Each live digest also says `"oldest": <ms>` — the sender's oldest post — so a newcomer learns how far back the forum goes. It then walks back from the digest window one **week** at a time with a **range request**:

```json
{"v":1, "t":"range", "since":<ms>, "until":<ms>, "have":["<short id>", "..."]}
```

"These are the posts I hold dated in [since, until)": peers send the rest, capped at 64 each. While a round still brings many new posts the same window is asked again; then the walk moves a week further back. A window where we already hold more than 400 posts is halved (down to one hour) so the request can list them all. Progress is saved (`history_floor`), and after an absence longer than 48 hours the walk restarts from the top, so the gap is filled too — windows already held complete in one round. **Missed → history** shows the progress and has *load older* (four more weeks, even past what peers advertised).

Peers answer one range request every 3 s at most, about at most two weeks at a time, and every answer draws on the re-send budget — a stream of requests can't turn a peer into a firehose. Posts arriving in answer to our own request skip the new-key budget, so history doesn't trickle in at the anti-flood rate.

---

## 8. Flood control

There is no server to throttle anyone, so every peer applies the same rules to what it accepts, and every client applies them to what it sends.

| Layer | Rule |
|---|---|
| Proof-of-work | Every post pays CPU time: 16 bits signed, 20 bits anonymous. |
| Per persona (checked by every peer) | At most **10 topics** and **60 replies** per hour, counted on the posts' own timestamps, so catching up after a day offline doesn't look like a flood. |
| Per account (checked by the sender) | The same hourly limits across *all* of an account's personas, including anonymous ones, so rotating doesn't lift them. The composer shows *"7 of 10 topics left this hour"* and disables posting at the limit. |
| Brand-new keys | Posts from keys never seen before draw from a shared budget (60 per minute, bursts of 120). A post that finds it empty isn't lost: the next digest offers it again. |
| Re-sends | Answers to digests are capped (240 per minute), so digests can't turn a peer into an amplifier. |
| Sizes | 16 KB per post, 24 KB per network message. |

Rate-Limiting Nullifiers (RLN), which Delivery v0.3 enforces on `logos.test`, could later replace the new-key budget with a per-member cryptographic limit.

---

## 9. Threat model

**Protects against**

| Threat | Protection |
|---|---|
| Linking someone's posts together | Rotation and anonymous posting; persona keys are independent |
| Linking personas to a real person through the app | No server, no email or phone, master key never published |
| Forged or altered posts | Ed25519 signature on every post, checked before storing |
| Censorship by one operator | No server; peer-to-peer relay; any peer holding a post can restore it for others |
| A stolen, switched-off device | Master key encrypted with an Argon2id-derived key |
| Cheap spam | Proof-of-work, hourly limits, new-key budget |

**Does not protect against**

| Limitation | Notes |
|---|---|
| Network-level metadata | All personas of one device publish from the same node; an observer of the network can correlate timing and IP. |
| Writing style | Rotation doesn't change how someone writes. |
| Links you click | Opening a link in a post visits that site from your own connection. Images and HTML in posts are never loaded, so merely reading a post contacts no one. |
| A compromised device while unlocked | Malware can read the unlocked key. |
| Deleting posts | Nothing published peer to peer can be reliably deleted, so Forumer doesn't offer delete. |
| Honest clocks | `ts` is set by the author; ordering is best-effort (future dates are capped). |

---

## 10. Design decisions and findings

| Decision | Why |
|---|---|
| **Plain relay, not reliable channels (SDS)** | SDS's causal ordering held back later messages until earlier ones arrived. The re-sent copy of a lost post was itself held back as "missing dependencies", so one lost post blocked repair. Digests over plain relay repair gaps reliably. The reliable-channel code path is kept behind a switch (`useChannels`). |
| **Digest repair instead of store queries** | It works with no store node and no server: any peer holding a post can restore it. |
| **History over Delivery, not Logos Storage (for now)** | We built a Storage probe (Settings → storage) and tested sharing a file between two instances with storage_module v2.1.2. Findings: (1) a node behind a home router reports *NotReachable* and never announces what it holds; (2) fetching a file's manifest only asks the DHT for providers, so even a peer we dialled directly can't be fetched from ("Failed to fetch manifest … after 10 attempts"); (3) the module's own download gives up on the manifest after 3 s. So files shared from home laptops can't be found by others. Retested on storage_module v3.0.0 (UPnP, relays, hole punching): the node takes ~2 min to start and its reachability goes *Unknown* → *NotReachable* on a home network, but sharing now **works** once both nodes have settled — two instances fetched each other's test file, in both directions, in 73–83 s (an attempt in the first minutes timed out). So Storage is usable, but slow to find content. History keeps travelling as posts over Delivery (seconds, crosses home routers); Storage is the path for media and history snapshots. |
| **Local SQLite, not Logos SQL** | The post log is per-device and private (it also holds the outbox). Logos SQL runs as a blockchain zone, and the blockchain module is out of scope for LP-0026. |
| **Own keys instead of `keystore_signer`** | Forumer needs keys derived from one master key and the ability to verify, not only sign. |
| **One topic for all domains** | No per-domain duplication, and the network doesn't learn what anyone reads. |
| **Two-level threads** | Deep nesting becomes unreadable; a second level gives every sub-conversation one clear start. |
| **Author-time hourly limits** | Counting arrival time would punish peers for catching up. |
| **One data folder per instance** | `LOGOS_USER_DIR` keeps accounts, posts and network identity separate per Basecamp instance. |

---

## 11. Repository layout

```
forumer/
├── flake.nix, metadata.json     Logos module build (logos-module-builder 0.3.2)
├── CMakeLists.txt
├── src/
│   ├── forumer.rep              contract between backend and view (Qt Remote Objects)
│   ├── forumer_backend.{h,cpp}  backend: delivery, timers, state for the view
│   ├── delivery_module_transport.{h,cpp}   delivery_module adapter (from the sample)
│   ├── base64.{h,cpp}
│   └── qml/                     Main, ForumStore, screens and components, fonts/
├── lib/forumer_core/            reusable engine (C++20, libsodium, SQLite, nlohmann/json)
│   ├── include/forumer_core/    account_store, bytes, crypto, flood, identity, mnemonic,
│   │                            post, post_store, sync, thread, vault
│   ├── src/
│   └── tests/                   110 unit tests
├── scripts/
│   ├── test-core.sh             build and run the unit tests
│   └── two-instances.sh         two isolated instances for end-to-end testing
├── docs/Forumer-description.md  original design document
├── LICENSE-MIT
└── LICENSE-APACHE-v2
```

---

## 12. Testing

| Layer | What | How |
|---|---|---|
| Unit | crypto, vault, recovery phrase, personas and rotation, account store, envelope signing and verification, PoW, post log and outbox, sync rules (digests, answers, retry backoff), thread placement, flood limits | `./scripts/test-core.sh` |
| End to end | post → received; nested replies; offline → catch-up; outbox retry; hourly limits; restore | `./scripts/two-instances.sh` (needs the live `logos.dev` network) |
| CI | GitHub Actions on every push: the `forumer_core` unit tests, then the full module build on Linux (Ubuntu 24.04) and macOS 14 | `.github/workflows/ci.yml` |

Backend logs are prefixed `[forumer backend]` on the host's stderr.

---

## 13. LP-0026 requirements

| Requirement | Status | How |
|---|---|---|
| No central server or service | ✅ | Peer-to-peer over Logos Delivery; all state on the device |
| Uses the Logos stack | ✅ / ⏳ | Delivery for posts, sync and history. Storage evaluated (see §10) and planned for media/snapshots; Chat planned |
| Accounts, topics, replies | ✅ | Several accounts; topics with domains; two-level replies |
| Reply by unique id, alias, or no id | ✅ | Persona / alias / anonymous per post |
| Privacy of one identity everywhere | ✅ | Keep / manual / auto rotation ([§5](#5-identity-and-privacy)) |
| Basecamp GUI with local build steps | ✅ | [§2](#2-quick-start) |
| Pre-built in a module catalog | ✅ | [`forumer-catalog`](https://github.com/bristinWild/forumer-catalog), built by the official release action ([§2](#2-quick-start)) |
| Usable by a non-expert | ✅ | Guided onboarding, search, clear sending / live / failed states |
| Past messages after being offline | ✅ | Store nodes (works with nobody else online), digest catch-up for 48 h, then week-by-week history back to the forum's first post ([§7](#7-network-and-sync)) |
| Failed sends kept for retry | ✅ | Persistent outbox, automatic and manual retry |
| Doesn't flood the network | ✅ | [§8](#8-flood-control) |
| Sound, reusable architecture | ✅ | `forumer_core` library, unit-tested, no UI dependencies |
| CI green | ✅ | [GitHub Actions](https://github.com/bristinWild/forumer/actions): unit tests + module build on Linux and macOS |
| README | ✅ | This document |
| "Program addresses" | n/a | Forumer deploys no on-chain programs (blockchain is out of scope for LP-0026). The content topic is listed in [§7](#7-network-and-sync). |
| Video demo | ✅ | [Demo video](https://youtu.be/Tc-MUpDcn7M) |
| FURPS self-assessment | ✅ | In the solution PR, `solutions/LP-0026.md` in logos-co/lambda-prize |
| MIT + Apache-2.0 | ✅ | `LICENSE-MIT`, `LICENSE-APACHE-v2` |

---

## 14. Roadmap

- [x] Accounts, password vault, recovery phrase, backup and restore
- [x] Personas, rotation, disclosure per post
- [x] Signed envelopes, proof-of-work, append-only post log
- [x] Topics, domains, follow, two-level replies, search
- [x] Outbox with automatic retry; offline catch-up through digests
- [x] Flood control and quota counter
- [x] Redesigned interface (night-city theme, light/dark)
- [x] Replies to you: unread badge, list, in-thread highlight, notice
- [x] Markdown formatting with toolbar and preview; user text never rendered as HTML
- [x] Shareable `basecamp://` links to threads and domains
- [ ] Media attachments through Logos Storage
- [ ] Private messages between personas
- [ ] Mute a persona / hide a post
- [x] CI (unit tests + module build on Linux and macOS)
- [x] Module catalog ([`forumer-catalog`](https://github.com/bristinWild/forumer-catalog))
- [x] Video demo, FURPS self-assessment
- [x] Full history for newcomers (week-by-week range requests over Delivery)
- [x] History from the network's store nodes, with nobody else online
- [x] 0.3 module stack (builder 0.3.2, delivery v0.3.0, storage v3.0.0); network choice logos.dev / logos.test
- [x] Storage file sharing between two nodes on storage v3 (both directions, 73–83 s per fetch)
- [ ] Storage between two different home networks
- Later: private forums with invite links; history snapshots and media through Logos Storage; RLN memberships for `logos.test` users

---

## 15. Credits and license

- Forked from [jzaki/forum-sample-app](https://github.com/jzaki/forum-sample-app) (MIT / Apache-2.0).
- Built on the [Logos](https://logos.co) stack. Fonts: Chakra Petch and Share Tech Mono (SIL Open Font License 1.1, `src/qml/fonts/OFL-*.txt`).

Dual-licensed under the **MIT License** and the **Apache License 2.0**, at your option.

> This is an independent community project. It has not been built for, on behalf of, or as part of the work of Logos or the Institute of Free Technology, and has not been reviewed, audited, approved or endorsed by them. The project is the sole responsibility of its contributor(s).