# Forumer for Android

**The same private, serverless forum, as an Android app.**

This document describes how Forumer will run on Android: what carries over from the Basecamp module, what changes, and why. An Android user and a Basecamp user see the same forum, on the same network, with the same posts, personas and private forums: the phone is one more peer, not a separate service.

> **Status:** design only. Nothing here is built yet, and the Basecamp module does not change behaviour because of it. Planned work is marked **(planned)**; questions not yet settled are collected in [§15](#15-open-questions).
>
> **Distribution:** an APK signed by Forumer's own key, outside Google Play and every other store ([§11](#11-distribution)).

---

## Contents

1. [Goals and scope](#1-goals-and-scope)
2. [What carries over](#2-what-carries-over)
3. [Architecture](#3-architecture)
4. [The network on a phone](#4-the-network-on-a-phone)
5. [The view on a phone](#5-the-view-on-a-phone)
6. [Images](#6-images)
7. [Foreground and background](#7-foreground-and-background)
8. [Security and privacy on Android](#8-security-and-privacy-on-android)
9. [Data on disk](#9-data-on-disk)
10. [Building](#10-building)
11. [Distribution](#11-distribution)
12. [Repository layout](#12-repository-layout)
13. [Testing](#13-testing)
14. [Roadmap](#14-roadmap)
15. [Open questions](#15-open-questions)
16. [References](#16-references)

---

## 1. Goals and scope

| Goal | What it means |
|---|---|
| **One forum** | Same content topics, wire messages, signed envelopes, proof-of-work and flood rules as the Basecamp module. Posts written on a phone verify on a desktop and the other way round. |
| **One engine** | `lib/forumer_core` is compiled for Android as it is. No second implementation of identity, posts or sync. |
| **No store, no Google services** | Installed from an APK. No Play Services, no Firebase push, no Google sign-in, no analytics. |
| **Minimal permissions** | Network access only in the first version. |
| **On the Logos stack** | The network is Logos Delivery, through its own C library, on the same `logos.dev` network as Basecamp. |

**Not in the first version:** iOS; attaching images and fetching full images (previews do show, [§6](#6-images)); running as a full relay node by default; staying connected in the background; push notifications.

---

## 2. What carries over

| Part | On Android |
|---|---|
| `lib/forumer_core` | **Unchanged.** C++20, libsodium, SQLite and nlohmann/json, all of which build with the Android NDK. All 150 unit tests apply. |
| Wire protocol | **Unchanged.** `/forumer/3/public/proto`, `/forumer/3/pf-<id>/proto`, the `post` / `digest` / `range` / `mask` / `mhere` / `mprev` messages, sealing for private forums. |
| Identity | **Unchanged.** Same vault format, recovery phrase, persona derivation, rotation policies and disclosure. A phrase written down on a desktop restores the same account on a phone. |
| Flood control | **Unchanged.** Same proof-of-work bits and limits. Peers can't tell a phone's post from a desktop's. |
| `ForumerBackend` logic | **Reused** after a refactor that moves the Basecamp-specific calls behind two small interfaces ([§3](#3-architecture)). |
| QML view (`src/qml`) | **Reused.** A small `logos` object stands in for Basecamp's ([§5](#5-the-view-on-a-phone)); the layout adapts to a narrow screen. |
| `delivery_module` | **Replaced** by `liblogosdelivery.so`, the C library of the same logos-delivery release (`v0.39.0`) that delivery_module v0.3.0 wraps. |
| `storage_module` | **Not in the first version.** Logos Storage has a C library but no Android build yet ([§6](#6-images)). |
| Basecamp intents and `basecamp://` links | **Replaced** by an Android intent filter for the same link format ([§5](#5-the-view-on-a-phone)). |

---

## 3. Architecture

### Components

```
┌─ Android app (one process) ──────────────────────────────────────────┐
│  QML view (src/qml, shared with Basecamp)                             │
│    ForumStore.qml ──── `logos` object (mobile/android/src)            │
│        │  direct calls on a QObject - no Qt Remote Objects link       │
│  ForumerBackend (C++, shared with Basecamp)                           │
│    ├─ forumer_core                                                    │
│    ├─ DeliveryPort ─▶ LibDeliveryPort ─▶ liblogosdelivery.so ──▶ Logos Delivery (logos.dev)
│    └─ StoragePort  ─▶ none (first version)                            │
└───────────────────────────────────────────────────────────────────────┘
```

Compared with Basecamp, where the view and the backend are separate processes talking over Qt Remote Objects (`src/forumer.rep`) and the backend reaches the network through `delivery_module` running as its own process, on Android everything is one process: the view calls the backend object directly, and the backend links the Delivery library directly.

### The refactor: ports

Today `ForumerBackend` inherits Basecamp's `LogosUiPluginContext` and calls `modules().delivery_module` and `modules().storage_module` directly. Those calls move behind two interfaces, with one implementation per host:

| Interface | Basecamp | Android |
|---|---|---|
| **`DeliveryPort`** | `BasecampDeliveryPort`: today's `DeliveryModuleTransport` plus the event wiring now in `bootstrap()` | `LibDeliveryPort`: `liblogosdelivery` |
| **`StoragePort`** | `BasecampStoragePort`: today's `storage_module` calls | `NoStoragePort`: reports Storage as unavailable |
| **Host** (data folder, start-up hook) | `LogosUiPluginContext`, `LOGOS_USER_DIR` | the Android app's `main()`, the app's private files folder |

`ForumerBackend` keeps its `.rep` base class (`ForumerSimpleSource`, generated by `repc`): on Android it is used only as a QObject with properties, signals and slots, never remoted. The Basecamp module must build and behave exactly as before after the refactor; this is phase 1 of the [roadmap](#14-roadmap).

### `DeliveryPort`: what Forumer uses, and where it lands

Every Delivery call the backend makes today, and its counterpart in `liblogosdelivery` (logos-delivery `v0.39.0`, `library/liblogosdelivery.h` and the generated `logosdelivery.h`):

| Today (`delivery_module`) | Android (`liblogosdelivery`) |
|---|---|
| `createNode(config)` | `logosdelivery_ctx_create(config)` - asynchronous, the context arrives in its callback |
| `start()` | `logosdelivery_ctx_start_node` |
| `subscribe(topic)` | `logosdelivery_ctx_subscribe` |
| `send(topic, payload)` → request id | `logosdelivery_ctx_send` with `{"contentTopic": …, "payload": "<base64>"}` → request id |
| `storeQueryAsync(…)` | `waku_store_query(query, peerAddr, timeoutMs)` (`liblogosdelivery_kernel.h`) |
| `getConnectionStatusAsync` | the `onConnectionStatusChange` event |
| `channelExists` / `channelCreate` / `channelSend` | not used: Forumer runs on plain relay (`useChannels = false`), so the Android port leaves channels out |
| events `messageReceived`, `messageSent`, `messagePropagated`, `messageError`, `connectionStateChanged`, `nodeStarted` | `logosdelivery_add_event_listener` for `onMessageReceived`, `onMessageSent`, `onMessagePropagated`, `onMessageError`, `onConnectionStatusChange`; start-up result from the `start_node` reply |

Library callbacks run on the library's own event thread. `LibDeliveryPort` copies what it receives and posts it to the Qt thread (`QMetaObject::invokeMethod`, queued), so the backend sees events on one thread exactly as it does in Basecamp.

---

## 4. The network on a phone

### Edge mode

A Delivery node runs in one of two modes (`logos_delivery/api/conf/modes.nim`):

| Mode | Protocols | Fits |
|---|---|---|
| **Core** | relay, and serves filter and light push to others; discovery | Basecamp today (`{"mode": "Core", "preset": "logos.dev"}`) |
| **Edge** | client only: receives through **filter**, sends through **light push**, reads history through **store**, all served by Core nodes | Phones |

The Android app runs **Edge** on `logos.dev`:

```json
{"mode": "Edge", "preset": "logos.dev"}
```

An Edge node keeps no relay mesh, so it uses far less battery and data, and it needs no open ports. Its service peers are Core nodes: the `logos.dev` fleet, and also every Basecamp user running Forumer, since Core mode serves filter and light push.

**(planned)** Core mode on a phone, offered as a setting for Wi-Fi and charging.

### What stays the same

Nothing in Forumer's sync depends on how a message travels, so the protocol is unchanged:

| | On a phone |
|---|---|
| Posts | Published by light push, received by filter, verified and stored exactly as on desktop. |
| Outbox | Signed and stored before sending; retried with the same backoff while the app is open. |
| Store history | Same queries to the same store nodes on joining (30 days, or the last day where a node caps it). |
| Digests | Same 48-hour digests, sent shortly after opening and on *Catch up*. |
| History walk | Same range requests. |
| Private forums | One filter subscription per forum topic; sealing unchanged. |

### What changes: data use

A phone in Edge mode receives everything on the topics it subscribes to, including other peers' digests and range requests, not only posts. A full digest is about 10 KB (512 short ids); each online peer sends one every 3 minutes, so with 30 peers each holding a busy 48 hours, that is about 6 MB an hour while the app is open. A quiet forum's digests are much smaller.

Proposed defaults for phones (**planned**):

| Behaviour | Default |
|---|---|
| Send our own digest | On opening, then every 3 minutes while open |
| Answer other peers' digests and range requests (re-send posts they lack) | Only on Wi-Fi - a setting, *help others catch up* |
| Walk history back week by week | Only on Wi-Fi, or on *load older* |

The lasting fix is a separate content topic for sync messages, so a phone can take posts live and catch up through the store; it changes the wire format and is listed in [§15](#15-open-questions).

### What changes: privacy

| Who | Learns | Compared with Basecamp (Core) |
|---|---|---|
| The filter service node | Your IP address and **which content topics you subscribe to**: the public topic and each `pf-<id>` - so it can link your IP to the ids of your private forums (not their names or content) | A relay peer sees the shard (`/waku/2/rs/3/4`), not content topics |
| The light-push service node | Your IP address and each message you send (sealed in a private forum) - so it can link your posts, under any persona, to your IP | Relay peers see that a message came from you, mixed with relayed traffic |
| The store node | Your IP address and the topics you ask about | Same as Basecamp |

So an Edge phone trusts its service nodes with more metadata than a desktop does. Ways to narrow it, **(planned)**: subscribe to different forums through different service peers; Delivery's **Mix** option for sends (`AnonymityLevel` *Preferred* / *Required*) once it runs on `logos.dev`; Core mode on Wi-Fi. The app says plainly which mode it runs in (Settings → network).

---

## 5. The view on a phone

### The `logos` object

The QML talks to Basecamp through a global `logos` object. The Android app provides its own, implemented in C++ and set as a context property, so `Main.qml` and `ForumStore.qml` don't change:

| Used in QML | Basecamp | Android |
|---|---|---|
| `logos.module("forumer")` | typed replica of the backend | the `ForumerBackend` object itself |
| `logos.watch(result, callback)` | waits for a remote call's reply | calls `callback(result)` at once: in one process a slot returns its value directly |
| `logos.isViewModuleReady("forumer")` and `onViewModuleReadyChanged` | whether the backend process is up | true once the backend has started |
| `onIntentRequested(requestId, intent, params, requester)` | Basecamp intents and `basecamp://` links | raised from an Android `VIEW` intent carrying a Forumer link |
| `logos.respond(…)` | answers an intent | does nothing |

Slots that do slow work (unlocking with Argon2id, mining proof-of-work) already run it on a worker thread inside a nested event loop that excludes user input (`runOffThread`). In one process this keeps the screen drawing - *signing…* animates - while taps are held until the work is done.

### Links

The app declares an intent filter for the existing link format, so a link copied on a desktop opens on a phone and the other way round:

```
basecamp://intent/forumer.open?p=<base64url JSON>
```

The payload is checked by the same `links.js` as on desktop: a 32-hex topic id with a day, a domain, or an invite (43-character key and a name). An invite is never joined without asking. Pasting a link into search (⌘K on desktop, the search field on a phone) keeps working as before.

### Layout **(planned)**

| Desktop | Phone (narrow screen) |
|---|---|
| Sidebar | Drawer, opened from the top bar |
| *All* and *Followed* side by side on Home | One column with a switch |
| Sheets (new topic, new private forum, join) | Full screen |
| Keyboard shortcuts | Not used; toolbar buttons instead |
| — | Android back button closes a sheet, then goes back a screen |

Fonts (Chakra Petch, Share Tech Mono) and both themes are bundled as on desktop.

---

## 6. Images

| | First version |
|---|---|
| **Previews** | **Shown.** Previews travel over Delivery (`mprev`) right after their post and come from store nodes too, so a phone shows them with no storage node, checked against the author's signature as on desktop. |
| **Full images** | Not fetched. The card says the full picture can be opened in Forumer on a desktop. |
| **Attaching** | Not offered. A post names its images by Storage CID, so attaching needs a storage node. |

The view decides whether to offer attaching from `canAttachImages`, which today checks only that the backend has an `attachImage` slot. The backend will publish a `storageAvailable` property, false on Android, and the view will use it instead.

**Later:** Logos Storage (`logos-storage-nim`) has a C library (`library/libstorage.h`) but no Android target. Porting it to the NDK, or another way to fetch images, is a separate step.

---

## 7. Foreground and background

### First version: while the app is open

| Event | What happens |
|---|---|
| App opened | Node created and started (Edge), topics subscribed, store history asked, first digest after a few seconds. |
| Post written while offline | Signed and kept in the outbox; sent when the node is up. |
| App in the background | Node keeps going for a short grace period, then stops. Android would stop its network soon anyway. |
| App opened again | Same as a desktop restart: store history, digests, outbox retries. Posts from while it was closed arrive within seconds. |

### Later **(planned)**

- **Stay connected:** an optional foreground service with a permanent notification, so the node keeps running with the screen off. Android 14 and newer require a service type; a *data sync* service is limited by Android 15 to 6 hours in any 24.
- **Reply notices:** shown as local notifications by that service. There is no push service, so with the service off a phone learns of replies when it opens.

---

## 8. Security and privacy on Android

| Area | Rule |
|---|---|
| **Cloud backup** | `android:allowBackup="false"` and data-extraction rules that exclude everything: the vault, `state.enc`, `own.enc` and the post log never go to Google's cloud backup or to a device-to-device transfer. A new phone restores from the recovery phrase. |
| **Screenshots** | `FLAG_SECURE` on the recovery phrase, backup and unlock screens, so they don't appear in screenshots or the recent-apps preview. **(planned)** A setting to apply it to the whole app. |
| **Clipboard** | An invite link is the forum's key. Android 13+ shows copied text in a preview; the app marks copied invites as sensitive (`ClipDescription.EXTRA_IS_SENSITIVE`, set through JNI because Qt's clipboard can't). |
| **Permissions** | `INTERNET` and `ACCESS_NETWORK_STATE` only. No storage, camera, contacts or location. The background service, when it comes, adds its foreground-service and notification permissions. |
| **Vault** | Same format: Argon2id (3 passes, 256 MiB) and XChaCha20-Poly1305. The cost settings are stored in each vault, so a phone can open a vault made on a phone or restore any account from its phrase. |
| **Android Keystore** | Not used. The password stays the only key; nothing ties the account to the device's hardware. Fingerprint unlock is an [open question](#15-open-questions). |
| **Lock** | Manual lock as on desktop. **(planned)** Lock automatically after some minutes in the background. |
| **Exported components** | Only the main activity, for the link intent filter. No content providers, no services exported. |
| **Links in posts** | Open in the phone's browser on tap, as on desktop (`MdText.qml`). Images and HTML in post text are never loaded. |

The threat model in the main README applies unchanged, plus the Edge-mode metadata in [§4](#4-the-network-on-a-phone).

---

## 9. Data on disk

When Basecamp doesn't set `LOGOS_USER_DIR`, the backend already uses Qt's `AppDataLocation` plus `/forumer`; on Android that is inside the app's private files folder, readable by no other app.

```
<app files>/forumer/            (0700; every file below is 0600)
├── accounts/
│   ├── accounts.json          accounts, their names, which one is selected
│   └── <account id>/
│       ├── vault.json         master key, encrypted with the password
│       ├── state.enc          rotation policy, persona counter, alias, followed domains, private forums (encrypted)
│       └── own.enc            which posts this account wrote, their send state, replies read (encrypted)
├── network                    the chosen network
├── peer_id                    a random id for this install
└── posts/
    └── posts.sqlite3          verified posts, previews, history progress (same as desktop)
```

No `storage/`, `media/` or `media-*` entries in the first version. The post log keeps the same 50,000-post cap.

---

## 10. Building

| Piece | Source |
|---|---|
| Android NDK and SDK | The version Qt for Android requires |
| Qt 6 for Android | Qt Quick, plus Qt Remote Objects only for the `repc`-generated base class. Qt is used under the LGPL v3, linked as shared libraries. |
| `forumer_core` | This repository, built with the NDK's CMake toolchain |
| libsodium | Built for Android with libsodium's own `dist-build` scripts |
| SQLite | The amalgamation, compiled in |
| nlohmann/json | Header only |
| `liblogosdelivery.so`, `librln.so` | logos-delivery `v0.39.0`: `make liblogosdelivery-android-arm64` with `ANDROID_NDK_HOME` set. The same Makefile also builds `x86_64` for the emulator. |
| TinyCBOR | Needed by the Delivery library's typed helpers (vendored by nim-ffi) |
| APK | Qt's `androiddeployqt` with Gradle |

| Target | Value |
|---|---|
| ABIs | `arm64-v8a` (phones), `x86_64` (emulator) |
| Minimum Android | 11 (API 30): logos-delivery builds its Android library for API 30 by default |

The build is a script, `mobile/android/build.sh` **(planned)**, that fetches the pinned versions and produces the APK. It is not part of the Nix flake in the first version.

---

## 11. Distribution

| | |
|---|---|
| Package | One APK, `forumer-<version>-android-arm64.apk` |
| Signing | Forumer's own release key (APK signature scheme v2/v3). The certificate's SHA-256 fingerprint is published in the README and with every release, so a copy passed from person to person can be checked. |
| Where | GitHub Releases, next to the `.lgx` packages. The APK can also be passed on directly - a file, a chat, a USB cable. |
| Install | The user allows installing from the app that opened the APK (a browser, a file manager), once. |
| Updates | A new APK signed with the same key installs over the old one, keeping the data. **(planned)** The app checks the releases page for a newer version only if the user turns this on. |
| Reproducible builds | **(planned)** Anyone can rebuild the APK from the tag and compare it. |

**Google's developer verification.** Since 30 September 2026, certified Android devices in Brazil, Indonesia, Singapore and Thailand check that apps come from a verified developer; Google plans to extend this to all certified devices worldwide in 2027. For an app from an unverified developer, users go through Google's *advanced flow*, or the developer uses a limited-distribution account (up to 20 devices), or registers with Google. Phones running Android without Google certification (GrapheneOS, LineageOS, /e/OS) are outside that scope. How Forumer handles this is an [open question](#15-open-questions).

---

## 12. Repository layout

The planned layout after phase 1 and phase 2; files that exist today keep their paths except where noted:

```
forumer/
├── lib/forumer_core/                  shared engine (unchanged)
├── src/
│   ├── forumer.rep                    contract between backend and view (unchanged)
│   ├── forumer_backend.{h,cpp}        shared backend, now host-independent
│   ├── ports/                         DeliveryPort, StoragePort interfaces (new)
│   ├── basecamp/                      BasecampDeliveryPort (today's delivery_module_transport),
│   │                                  BasecampStoragePort, plugin glue (moved)
│   ├── image_prep.{h,cpp}, base64.{h,cpp}
│   └── qml/                           shared view
├── mobile/android/                    (new)
│   ├── CMakeLists.txt
│   ├── build.sh                       fetch pinned dependencies, build the APK
│   ├── src/                           main.cpp, the `logos` object, LibDeliveryPort, NoStoragePort,
│   │                                  Android glue (intents, clipboard, FLAG_SECURE)
│   ├── android/                       AndroidManifest.xml, data-extraction rules, icons
│   └── third_party/                   built liblogosdelivery, librln, libsodium (not checked in)
└── docs/Forumer-android.md            this document
```

---

## 13. Testing

| Layer | What | How |
|---|---|---|
| Unit | `forumer_core`'s 150 tests, cross-compiled | Run on a device or emulator with `adb push` and `adb shell` **(planned)** |
| Unit | `LibDeliveryPort`: event parsing, request ids, thread hand-off | Desktop build against the Linux `liblogosdelivery` **(planned)** |
| Basecamp | The refactor changes nothing | Existing unit, view and CI tests stay green |
| End to end | Desktop ↔ phone on `logos.dev`: post and reply both ways; private forum created on one, joined on the other; post offline on the phone, reopen; open a shared link; restore an account from its phrase on the phone | `./scripts/two-instances.sh` plus an emulator or phone |
| CI | Build the APK on Linux | GitHub Actions **(planned)** |

---

## 14. Roadmap

- [ ] **Phase 1 - ports.** `DeliveryPort` and `StoragePort`; Basecamp implementations; the Basecamp module unchanged, CI green.
- [ ] **Phase 2 - Android shell.** Qt app, the `logos` object, `LibDeliveryPort` in Edge mode: accounts, personas, topics, replies, domains, private forums, links, outbox, store history, digests.
- [ ] **Phase 3 - phone layout.** Drawer, one column, full-screen sheets, back button.
- [ ] **Phase 4 - security details.** Backup rules, `FLAG_SECURE`, sensitive clipboard, auto-lock.
- [ ] **Phase 5 - release.** Signed APK on GitHub Releases, published certificate fingerprint, build script.
- Later: digest and history settings for mobile data; stay-connected service with local reply notices; Core mode on Wi-Fi; Mix for sends; images through Logos Storage; reproducible builds.

---

## 15. Open questions

| # | Question | Notes |
|---|---|---|
| 1 | **Digest traffic on mobile data** | Every peer's digest reaches every phone on the topic. A separate sync topic (posts on one, digests and range requests on another) would let phones skip it, but changes the wire format: every peer has to understand it, so it waits for a format version change. |
| 2 | **Should phones repair others' gaps?** | Answering digests costs a phone data and battery; desktops do it better. Proposed: Wi-Fi only ([§4](#4-the-network-on-a-phone)). |
| 3 | **Edge metadata** | One filter node sees all of a phone's forum topics. Spreading subscriptions over several service peers, and Mix, would narrow this. |
| 4 | **Argon2id on low-memory phones** | 256 MiB is fine on most phones; on low-end ones it may be killed. The vault stores its own cost, so a phone could use less - weaker against an attacker who has the device. |
| 5 | **Fingerprint unlock** | Convenient, but it puts a key in the device's Keystore, tying the account to the hardware. |
| 6 | **Store replies through the library** | `waku_store_query` returns the store response as JSON; it should be the same shape `DeliveryModuleTransport::parseStorePage` reads from `delivery_module`. To confirm in phase 2. |
| 7 | **Google verification** | Use the advanced flow (users' friction), register as a developer (identity with Google), or target non-certified Android only. |
| 8 | **Qt or a native UI** | Qt reuses all of `src/qml`. A native UI would fit Android better but means a second view to keep in step. This document assumes Qt. |

---

## 16. References

- logos-delivery `v0.39.0`: `library/liblogosdelivery.h`, `library/README.md`, `library/MESSAGE_EVENTS.md`, `logos_delivery/api/conf/modes.nim`, the Makefile's `liblogosdelivery-android-*` targets, `examples/mobile` - [github.com/logos-messaging/logos-delivery](https://github.com/logos-messaging/logos-delivery)
- logos-delivery-module `v0.3.0` (pins logos-delivery `v0.39.0`) - [github.com/logos-co/logos-delivery-module](https://github.com/logos-co/logos-delivery-module)
- logos-storage-nim: `library/libstorage.h` - [github.com/logos-storage/logos-storage-nim](https://github.com/logos-storage/logos-storage-nim)
- Android developer verification - [developer.android.com/developer-verification](https://developer.android.com/developer-verification)
- Forumer's own design and behaviour: [`README.md`](../README.md)

> This is an independent community project. It has not been built for, on behalf of, or as part of the work of Logos or the Institute of Free Technology, and has not been reviewed, audited, approved or endorsed by them. The project is the sole responsibility of its contributor(s).