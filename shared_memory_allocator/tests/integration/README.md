# Integration Tests

These integration tests exercise end-to-end open/commit/recovery flows, crash points, and data consistency invariants.

Note:

- `cm_close(...)` unlinks the shm object by design, so persistence/recovery scenarios should model crash/kill restart behavior rather than clean close/reopen on the same name.

## Test binaries

- `cm_integration_crash_recovery_test` (`test_crash_recovery.c`)
  - Smoke test for open + commit lifecycle.

- `cm_integration_checkpoint_flow_test` (`test_checkpoint_flow.c`)
  - Opens shared memory, executes named checkpoints (`after_open`, `after_commit`), and commits.
  - Supports deterministic fault injection via `CM_CRASH_POINT`.

- `cm_integration_reopen_consistency_test` (`test_reopen_consistency.c`)
  - Verifies destructive close semantics (`cm_close` unlinks shm object).

- `cm_integration_crash_matrix_test` (`test_crash_matrix.c`)
  - Executes crash-point matrix across undo/copy/commit/recovery boundaries.

- `cm_integration_named_lifecycle_recovery_test` (`test_named_lifecycle_recovery.c`)
  - Validates named object lifecycle recovery (`initializing -> ready`) after crash.

- `cm_integration_commit_persists_test` (`test_commit_persists.c`)
  - Verifies committed bytes persist across crash/restart.

- `cm_integration_realloc_free_commit_persists_test` (`test_realloc_free_commit_persists.c`)
  - Verifies committed `realloc/free` allocator metadata and data persist across crash/restart.

- `cm_integration_uncommitted_rolls_back_test` (`test_uncommitted_rolls_back.c`)
  - Verifies uncommitted bytes roll back to last commit after crash/restart.

- `cm_integration_realloc_free_uncommitted_rolls_back_test` (`test_realloc_free_uncommitted_rolls_back.c`)
  - Verifies uncommitted `realloc/free` allocator changes roll back after crash/restart.

- `cm_integration_multi_page_cow_test` (`test_multi_page_cow.c`)
  - Verifies rollback correctness across many dirtied pages.

- `cm_integration_recovery_idempotence_test` (`test_recovery_idempotence.c`)
  - Verifies crash-during-recovery idempotence and final undo-log drain.

## Helper apps

- `apps/simple_writer_app.c`
  - Skeleton writer-style app that opens and commits.

- `apps/simple_verify_app.c`
  - Skeleton verifier-style app that re-opens state.

## Tooling Scaffold

- `tools/cm_dump_shm.c` (build target: `cm_dump_shm`)
  - Stub CLI for inspecting shm metadata/layout directly.

## Run

From `libs/shared_memory_allocator`:

```bash
ctest --preset dev-debug -R cm_integration --output-on-failure
```

Optional direct checkpoint invocation:

```bash
CM_CRASH_POINT=after_open ./build/dev-debug/cm_integration_checkpoint_flow_test
```
