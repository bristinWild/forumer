# Forumer security

Version 0.2.4 (pre-release). This document maps each security threat to the defence that addresses it, the test that checks that defence, and what is still not covered. Read it as a list of claims: if a claim has no test, it says so.

Every test name below is in `lib/forumer_core/tests/` unless marked otherwise. Run them with `./scripts/test-core.sh`. The QML sanitiser and link tests are in `scripts/test-markdown.mjs` and `scripts/test-links.mjs`.

## 1. Scope and assumptions

**What Forumer is.** A peer-to-peer forum on the Logos network, running as a Logos Basecamp module. There is no server, no account registration, and no email or phone number.

**Trust assumptions.**

- Other peers are untrusted. Any peer can send anything, sign with any key it generates, and send at any rate.
- The device is trusted while an account is unlocked. Malware with access to an unlocked device can read the keys in memory.
- The Basecamp host and the Logos modules it loads (delivery, storage) are trusted to run as documented.
- The network is observable. An observer can see traffic timing and the IP address of the node that sends it.

**Who we defend against.**

| Adversary | Capability |
|---|---|
| Spammer | Sends many posts, from many new keys, and mines proof-of-work. |
| Flooder | Sends digests, range requests or mask lookups as fast as it can, to make honest nodes do work. |
| Network observer | Watches who publishes what, when, and from which address. |
| Curious local user | Can read files in the data folder, or the diagnostic log. |
| Device thief | Has the device, possibly locked. |
| Key thief | Has a persona key, or the recovery phrase. |

## 2. Reading the tables

Each row gives:

- **Threat:** what could go wrong.
- **Defence:** what Forumer does about it.
- **Test:** the test that checks the defence. "Manual" means there is no automated test yet.
- **Limit:** what the defence does not cover.

## 3. Identity and privacy

| Threat | Defence | Test | Limit |
|---|---|---|---|
| Linking one person's posts together | Personas are derived from the master key and are independent of each other. Rotation policy can be Keep, Manual or Auto (a new persona per post). Anonymous posts use a one-time key. | `identity_personas_are_deterministic_and_distinct`, `identity_auto_uses_a_new_persona_every_post`, `identity_anonymous_is_always_fresh_and_keeps_current`, `f1_anonymous_keys_dont_use_the_persona_counter` | Writing style is not changed by rotation. Network-level timing and IP are not hidden (see §8). |
| Linking a persona to a real person through the app | No server, no email or phone, no account registry. The master key is never published. | Manual (design property) | Anyone who knows a persona's posts can study them. |
| Anonymous posts reused across posts | Each anonymous post signs with a fresh one-time key. | `identity_anonymous_is_always_fresh_and_keeps_current`, `f1_restore_on_a_fresh_device_never_reuses_an_anonymous_key` | None known. |
| Persona numbers reused after a restore | A restored account skips personas already in use. | `f1_skip_known_never_hands_out_a_used_persona`, `identity_restore_from_phrase_recovers_personas_and_counter` | None known. |
| Restore on the wrong account | The restore flow reports what was restored and never overwrites another account's data. | `store_restore_existing_account_keeps_settings_and_resets_password`, `store_restore_reports_bad_phrase` | None known. |

## 4. Post integrity

| Threat | Defence | Test | Limit |
|---|---|---|---|
| Forged post | Every post is signed (Ed25519) by its persona, and the signature is checked before storing. | `post_sign_and_verify`, `post_tampering_is_detected` | None known. |
| Post moved to another persona | The signature covers the signing persona. | `post_cannot_be_reattributed_to_another_persona` | None known. |
| Altered post in transit | Any change to the signed content breaks the signature. | `post_tampering_is_detected`, `aead_round_trip_and_tamper_detection` | None known. |
| Duplicate post IDs | IDs are unique per signing. | `post_ids_are_unique_per_signing` | None known. |
| Post without work (spam) | Proof-of-work on every post: 16 leading zero bits for personas, 20 for anonymous posts. Enforced when a post arrives. | `post_proof_of_work_is_enforced` | Proof-of-work is a cost per post, paid by the author's CPU. It is **not** a per-person or per-sender rate limit: an attacker can make new keys and pay the cost again. See §5. |
| Oversized post | A post is capped at 16 KiB encoded. | Manual (limit in `post.h`) | No dedicated test yet. |
| Oversized sync message | A sync message is capped at 24 KiB. | `sync_full_digest_fits_payload_limit` | No dedicated oversize-rejection test. |
| Post dated far in the future | Timestamps more than 10 minutes ahead of our clock are capped. | `phase_a_range_future_end_is_clamped_to_skew` (range requests only) | Timestamps are chosen by the author. Ordering is best-effort. |

## 5. Flood control and unsigned traffic

Forumer has no server to throttle senders, so every peer applies the same limits to what it accepts and every client applies them to what it sends. The layers are listed in `lib/forumer_core/include/forumer_core/flood.h`.

**Layers that apply to signed posts:**

| Layer | Limit | Test |
|---|---|---|
| 1. Proof-of-work | 16 bits (persona), 20 bits (anonymous) | `post_proof_of_work_is_enforced` |
| 2. Per persona, author time | 10 topics and 60 replies per hour; 40 topics and 240 replies per 24 hours | `flood_limits_per_kind`, `flood_store_counts_by_author_and_window` |
| 2a. Live arrivals, our clock | Twice the hourly limit | `f6_arrival_limit_counts_by_our_clock` |
| 2b. History and store-node posts | Per-day limit by author time | Manual (no dedicated test found) |
| 3. New keys | Shared budget: 60 per minute, burst 120. A priority lane (30 per minute, burst 60) for posts with at least 18 bits of work. | `flood_token_bucket`, `flood_token_bucket_ignores_clock_going_back` |
| 4. Per account, on the sender | Same hourly limits, counted across all personas and anonymous posts from this device | `flood_store_counts_own_posts_across_personas` |
| 5. Re-sending to repair peers | 240 per minute, burst 128 | Manual (no dedicated test) |

**Layers added in 0.2.4 for unsigned traffic (review of 0.2.3):**

These protect against messages that carry no signature, such as digests, range requests and mask lookups. The last column says which defences were reproduced on the code before 0.2.4.

| Threat | Defence | Test | Reproduced on old code? |
|---|---|---|---|
| Digest flood makes the node read its whole held set | A digest reads the held set at most once, and only while the digest scan budget (60 per minute, burst 120) lasts. | `phase_a_digest_with_spent_budget_reads_nothing`, `phase_a_digest_reads_held_set_once_when_prompting`, `phase_a_digest_reads_held_set_once_without_prompt` | Yes, by a mutant that ignores the budget (the test fails). |
| Empty range request starts a cooldown and silences honest requests | The range cooldown starts only when an answer is actually queued. An empty valid window does not start it. | `phase_a_empty_answer_does_not_start_the_gap`, `phase_a_answered_range_starts_the_gap`, `phase_a_invalid_range_does_not_start_the_gap` | Yes. The empty-answer test fails on the old gate. |
| Range request asks for an unbounded span or a future end | Ranges are clamped to the clock-skew limit and trimmed to the newest span. | `phase_a_range_future_end_is_clamped_to_skew`, `phase_a_range_span_is_trimmed_to_newest` | Not checked. |
| Mask lookup flood causes database work and queued previews | The MediaAsk budget (30 per minute, burst 60) is taken before any preview lookup, queued preview, or storage `exists()` call. | `phase_a_mediaask_budget_bounds_distinct_lookups` (bucket only) | Partly. The handler ordering was reproduced by code reading. The core test covers only the bucket. The handler ordering needs the two-instance smoke test. |
| Digests from peers that are older than our history limit | Ignored. | `f10_digest_oldest_before_forumer_is_ignored` | Not checked. |

**Limits of flood control:**

- New keys are cheap to make. Layers 1 and 3 slow a spammer down; they do not stop one.
- Layer 5 and the digest budgets are per node. A flooder can still use many nodes.
- Transport-level rate limiting is not implemented. The Delivery module's own limits are not controlled by Forumer.
- Rate-Limiting Nullifiers (RLN) in Logos Delivery v0.3 would replace layer 3 with a cryptographic per-member limit. Not yet used.

## 6. Media and images

| Threat | Defence | Test | Limit |
|---|---|---|---|
| Image signed by someone else | Media is accepted only if the author signed it. | `media_refuses_what_the_author_did_not_sign` | None known. |
| Private-forum image readable by outsiders | Images in a private forum are sealed with that forum's key. | `media_private_forum_images_need_the_forum_key`, `media_seal_and_open_round_trip` | None known. |
| Malicious holder hint sends us to an unsafe address | Holder hints are checked before we dial them. | `media_holder_hints_refuse_what_we_would_not_dial`, `media_holder_hints_round_trip` | None known. |
| Preview tampered with or swapped | Previews travel in signed, sealed pieces, and each piece is checked. | `media_previews_travel_in_signed_sealed_pieces`, `media_preview_pieces_are_checked`, `media_previews_are_kept_and_go_with_their_forum` | None known. |
| Fetching a reference that was not checked | References are checked before fetching. | `media_references_are_checked_before_fetching`, `media_references_survive_signing` | None known. |
| Unsupported or oversized image | Only allowed image types, within size limits, are accepted. | `media_only_allowed_images_within_the_limit` | WebP decoding is not tested in CI (see §8). |
| Re-hosting reveals who holds an image | Re-hosting can be turned off in settings. | Manual | The serving node sees your storage node's address when someone fetches an image. |

## 7. Private forums

| Threat | Defence | Test | Limit |
|---|---|---|---|
| Outsider reads a private forum | A private forum is its own 32-byte key. Everything derives from it: the forum ID, the wire key that seals messages on the topic, and the store key that seals posts at rest. | `forums_create_and_rebuild_from_the_key`, `forums_wire_messages_are_sealed_and_bound_to_the_forum`, `forums_posts_are_sealed_at_rest_and_kept_apart_from_public_sync` | Anyone with the invite link can read the forum and post to it. |
| Invalid forum name or key accepted | Names and keys are checked. | `forums_names_are_checked` | None known. |
| Leaving a forum leaves its posts behind | Leaving a forum drops its posts from this device. | `forums_leaving_drops_the_forums_posts` | Posts already seen by others stay on the network. |
| Invite link leaked | Not defended. | Manual | Links cannot be revoked. Replace the forum instead. |

## 8. Local data and secrets

| Threat | Defence | Test | Limit |
|---|---|---|---|
| Stolen device, locked | The master key is encrypted with an Argon2id-derived key (libsodium `crypto_pwhash`, parameters in `crypto.cpp`). | `password_key_depends_on_password_and_salt`, `store_rejects_short_passwords`, `vault_detects_tampering` | Security depends on the password's strength. |
| Account state readable on disk | Account state is encrypted at rest. | `f3_account_state_is_encrypted_at_rest` | Only while the device is locked. An unlocked device can be read (see §1). |
| Post log links posts to an account | The post log holds no account link. | `f3_post_log_file_holds_no_account_link` | None known. |
| Other users on the same machine read the post log | The post log and data files are created owner-only. | `f3_post_log_is_owner_only` | Depends on the operating system's file permissions. |
| Seed phrase leaks through the clipboard | The phrase screen copies the phrase on request, clears the clipboard after 60 seconds (only if it still holds the phrase), and clears it when the screen closes. | Manual (B2) | Clipboard managers and other apps may keep copies even after clearing. The screen warns about this. |
| Seed phrase mistyped or swapped | The 24-word phrase has a checksum. | `mnemonic_checksum_catches_swapped_words`, `mnemonic_reports_unknown_word_and_position` | None known. |
| Unlock with a weak password | Passwords shorter than the minimum are refused. | `store_rejects_short_passwords` | Minimum length, not strength. |

## 9. Logs and diagnostics

| Threat | Defence | Test | Limit |
|---|---|---|---|
| Diagnostic log links posts, personas or forums to a device | Every log line is scrubbed before it is written. Any run of 20 or more letters and digits is shortened to its first 8 characters and "...". This covers post IDs, CIDs, forum IDs, keys and fingerprints. | `phase_a_scrub_shortens_every_long_identifier`, `phase_a_log_refs_hide_the_tail_of_identity` | Scrubbing is by shape, not by meaning. A short identifier that is under 20 characters is not scrubbed. |
| A full identifier reaches a log through a call site that bypasses the scrubber | Not possible in the backend: all logging goes through `logEvent`. | Manual, plus `scripts/check-logs.sh` on smoke-test logs | The check covers the smoke-test logs only. It is not run in CI yet. |

## 10. Content and display

| Threat | Defence | Test | Limit |
|---|---|---|---|
| HTML or script in a post runs in the app | Post text goes through a sanitiser. HTML is escaped. Escaped backslashes do not reopen markup. | `scripts/test-markdown.mjs` | None known. |
| Malicious link in a post | Only http and https links are kept. Other schemes keep only their text. | `scripts/test-links.mjs` | Following a link visits that site from your own connection. |
| Remote image in a post contacts an outside server | Images and HTML in posts are never loaded, so reading a post contacts no one. | `scripts/test-markdown.mjs` | None known. |

## 11. Known limitations

These are stated plainly so they can be checked and fixed later.

- **No deletion.** Nothing published peer to peer can be reliably deleted. Forumer has no delete feature for published posts. Test posts from development are still on the public forum.
- **Proof-of-work is not a per-person limit.** See §4 and §5.
- **Network-level metadata.** All personas on one device publish from the same node, so an observer of the network can correlate timing and IP address.
- **No forward secrecy.** A stolen persona key decrypts past direct messages sent to it. A ratchet is future work.
- **Clocks are the author's.** Ordering is best-effort.
- **Storage is slow to find content.** Image and history transfer over the Storage module can take about 2 minutes to settle on a home network.
- **Transport-level rate limiting is not implemented.**
- **WebP decoding is not tested in CI.** Only PNG and JPEG are covered in the planned CI image test.
- **The cost of `storage_module.exists()` has not been measured.** The MediaAsk budget bounds how often it is called, but not how long each call takes.
- **Manual checks.** Clipboard behaviour, the phrase screen, and the link opening flow have no automated test.

## 12. Verifying

```
./scripts/test-core.sh               # core unit tests (163)
node scripts/test-markdown.mjs       # post text sanitiser
node scripts/test-links.mjs          # link allow-list
./scripts/check-logs.sh <backend.log>   # no full identifiers in a smoke-test log
```

Run `check-logs.sh` on the logs of two instances after posting from each. A pass means every identifier in the log was scrubbed.

## 13. Status of the 0.2.3 review findings in 0.2.4

| Finding | Status in 0.2.4 |
|---|---|
| A1, digest database work before the limit | Fixed and tested (§5). |
| A2, empty range starts the cooldown | Fixed and tested (§5). |
| A3, mask lookup flood | Fixed. Ordering is reproduced by code reading. The handler ordering needs the smoke test (§5). |
| A4, own-post timestamps | No change. The review did not reproduce a defect. Changing timestamp semantics could expose locally authored posts. |
| A5, full identifiers in logs | Fixed and tested (§9). |
| A6, reproduction record | Recorded in this section per finding. |

## 14. Reporting a vulnerability

Contact - bristineth@gmail.com 
x - borah_bristin