# Reelcut posting into Forumer: what was tested

Date of the live test: 19 October 2026 (India time).

This records one outside app posting into Forumer, what was seen, and what was not shown. It is evidence for the Supportability and interoperability criteria, not a claim that the integration is finished.

## What Reelcut is

[Reelcut](https://github.com/vpavlin/reelcut-basecamp) is a Basecamp app by vpavlin that cuts a long video into short captioned vertical reels. Its PR [#7](https://github.com/vpavlin/reelcut-basecamp/pull/7) (merged 18 October 2026) adds posting the reels as replies in a forum thread. It supports Forumer and Logos Forum. For Forumer it writes wire v1 itself, on `/forumer/3/<forum>/proto`, signed with its own Ed25519 key under an alias, with the proof of work Forumer asks of a new key. Forumer needed no change for this. The integration requests that came out of it are in the integration issue: https://github.com/bristinWild/forumer/issues/9

## Setup

| Part | Version or setting |
|---|---|
| Forumer | 0.2.5 (commit `7533c2a`), installed as a local `.lgx` |
| Basecamp | 0.3.1 Portable, macOS |
| Reelcut | v0.2.0, running through Docker (`ghcr.io/vpavlin/reelcut:latest`) |
| Input video | a local 1 minute 19 second file, 179 words |
| Reels cut | two: 28 s and 21 s |
| Reelcut alias | "Reelcut", key fingerprint `fr:2PBX-QP9G` |
| Forum | `public` |
| Thread link | the `basecamp://intent/forumer.open?p=...` link from Forumer's "copy link" |

## Wire-level check

Reelcut's own test (`core/tests/forum_check`) builds against Forumer's `forumer_core` and the other forum's library. Run against Forumer `7533c2a`, all 29 posts Reelcut wrote were accepted by Forumer's decoder and verifier. That was run once, in a clean checkout, before the live test.

## Live test

1. Pasted a Forumer thread link into Reelcut. Reelcut showed `Thread: 287fef7a757af128a6a5e38170385956`, the id inside the link.
2. Posted both reels with Logos Storage off.
3. Turned on Logos Storage ("This computer's node"), uploaded the reels, and posted both again.

## What was seen

- **Posts arrive.** Both reels appeared as replies in the Forumer thread, authored "Reelcut", with the title and description for each reel and a line such as "[28 s reel]".
- **Another instance and account received them.** Opened on a second Forumer instance under a different account, the replies from the first run were there too. This was checked for the first run only.
- **With Storage on, the replies carry CIDs.** Each reply gave its own reel's CID and the CID of the set, after the words "Watch it from Logos Storage".
- **The CIDs match real files.** In the Basecamp Storage app the node lists `01-wires-can-t-replace-meetups.mp4` (video/mp4, 9.1 MB), `02-logos-revives-civil-society.mp4` (video/mp4, 4.7 MB) and `reels-c7a37c81.json` (501 B, the set manifest), and their CIDs match the ones in the replies. Short forms: `zDvZRwzk...rvfREtcVuqtB`, `zDvZRwzm573...aEQn9ija6pN` and, for the set, `zDvZRwzm52M...`. Full values are in the screenshots.
- **Forumer's checks were not bypassed.** The replies went through the normal signed-post path, so signature and proof of work were accepted.

Screenshots (save in `docs/e3/img/`):

| File | Shows |
|---|---|
| `reelcut-post-screen.png` | Reelcut with Forumer chosen and the thread id shown |
| `forumer-reelcut-replies.png` | the Reelcut replies in the thread, text only |
| `forumer-reelcut-replies-cids.png` | the replies with per-reel and set CIDs |
| `storage-manifests.png` | the Storage app listing the two videos and the manifest |

## What this does not show

- **Playing the video.** Forumer shows the CIDs as plain text. There is no player, and Forumer attaches only JPEG, PNG and WebP images, so a video cannot be played in a thread.
- **Fetching from another node.** The files were uploaded by this machine's own node, so seeing them in its Storage app is expected. A reel has not yet been fetched by a different node from its CID. During the test the Storage app's Node panel said "Not reachable" while also listing peers. That status is unexplained.
- **The second run on a second instance.** Only the first run was checked there.
- **Select and copy.** In 0.2.5 the CIDs in a thread cannot be selected with the mouse, so they cannot be copied out of Forumer.
- **Scale and platforms.** One run, one machine, two reels, macOS only. Reelcut posted two replies, far below the per-key limits (10 topics and 60 replies an hour).

## Follow-ups

For 0.2.6, in this order:

1. Make post text selectable, so a CID can be copied.
2. The compose intent requested in the integration issue (it would only prefill the composer, never post by itself).
3. Investigate showing a Storage video from a CID in a post. This needs a check first that Basecamp's plugin sandbox can play video.
