# E3: a post written offline survives a restart and reaches another node

Run on 2026-10-18 by hand, two isolated instances (`./scripts/two-instances.sh`)
on one macOS machine, against the live `logos.dev` network. Build: branch
`hardening/v0.2.5` (version 0.2.5). Synthetic test accounts only.

## Scenario

1. Instances A and B join the same private forum; a baseline post from A reaches B.
2. Wi-Fi off (both instances lose the network).
3. A writes a topic. It is held in **Missed -> outbox** as *sending...*.
4. A is quit completely, then started again, still offline, and unlocked.
5. Wi-Fi on.
6. B receives the topic.

## What the logs show

Times are local (`backend.log` of each instance). The post is shown by its short
reference `fcf0b78e`.

| Time | Instance | Event |
|---|---|---|
| 15:28:15 to 15:28:19 | A | baseline post: `send outcome: propagated`, then `sent` |
| 15:29:32 | A | `connection state -> Disconnected` (network off) |
| 15:30:11 | A | `retrying fcf0b78e (attempt 2)` while offline |
| 15:32:48 | A | application started again, still `Disconnected` |
| 15:32:58 | A | `retrying fcf0b78e (attempt 3)`: same post, counter carried over the restart |
| 15:33:49 | A | `connection state -> Connected` |
| 15:33:50 | B | `received fcf0b78e` (about one second after A reconnected) |
| 15:33:54 | B | `media: preview of fcf0b78e:0 arrived` |

Both logs pass `./scripts/check-logs.sh` (no full identifiers).

## What this establishes

- A post written while the sender was offline was kept across an application
  restart (the retry attempt counter continued from 2 to 3 for the same post).
- It reached another node about one second after the sender's connection
  came back, with no action from the user other than restoring the network.

## What this does not establish

- The log does not say which path delivered the post at 15:33:50: the outbox
  retry, or the peers' digest repair (B logged sending its digest just before
  `received`). Either path is a valid way for the post to arrive; the run does
  not separate them.
- No `send outcome: sent` line was logged for this post after reconnecting, so
  the run does not show the outbox entry being marked sent.
- Run by hand, once, on one machine, against a shared network. A forced kill
  (`kill -9`) of the sender was not tested.
