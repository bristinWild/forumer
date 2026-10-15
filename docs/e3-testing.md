
# E3 - Forumer Integration Testing

Version: 0.2.4

## Objective

Verify that Forumer operates correctly using repeatable
automated tests rather than manual GUI testing alone.

The final objective is a multi-node test suite covering
Logos Delivery, local persistence, identity management,
offline recovery and private forums.

## Milestone 1 - Headless UI smoke tests

Status: Implemented, pending successful execution.

Tests:
- Forumer launches successfully.
- Onboarding screen loads.
- ForumStore object exists.
- Account recovery navigation works.
- User can return to account creation.

Test file:
`tests/ui-tests.mjs`

### Run

The project pins logos-module-builder v0.3.2.

```bash
nix build .#integration-test --out-link result-e3 -L
cat result-e3/integration-test.log
```

The test runner launches a standalone Logos host,
runs QML tests headlessly and reports failures.

This does not yet test network communication.

## Milestone 2 - Multi-node integration

Status: Planned.

Run at least two independent Forumer nodes with
separate data folders, accounts and network identities.

Scenarios:

1. Start Node A and Node B.
2. Create a topic from Node A.
3. Verify Node B receives the same signed post.
4. Reply from Node B.
5. Verify Node A receives the reply.
6. Stop Node B.
7. Publish another topic from Node A.
8. Restart Node B and verify catch-up.
9. Test pending posts and outbox persistence.
10. Verify synchronization across private forums.

Compare real post IDs and content, not merely UI labels.

## Milestone 3 - Privacy and security tests

Status: Planned.

Scenarios:

- Anonymous signing keys are not reused.
- Persona rotation works.
- Private forum outsiders cannot decrypt posts.
- Different accounts maintain independent state.
- Unsigned traffic budgets limit database work.

Use synthetic test accounts only.

Never upload recovery phrases or private forum
invite secrets to CI artifacts.

## Milestone 4 - Logos testnet 0.4

Status: Not verified.

LP-0026 requires an end-to-end test on Logos
testnet 0.4.

Once access and compatible module versions are
available, repeat the multi-node scenarios there.

Record:
- Basecamp version
- Delivery module version
- Storage module version
- Testnet configuration
- Test date
- Actual results
- Logs with sensitive data removed

## Completion criteria

E3 is complete only when:

- Headless UI tests pass.
- Two or more independent nodes exchange posts.
- Offline recovery and outbox persistence pass.
- Private forum behavior is verified.
- Tests produce repeatable pass/fail results.
- Sanitized logs are available for inspection.

A successful single-node smoke test is not
equivalent to completed E3.
