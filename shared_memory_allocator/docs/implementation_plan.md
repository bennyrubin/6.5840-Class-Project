# Implementation Plan: userfaultfd CoW + Undo Log + Recovery (Minimal v1)

## 1. Goal

Implement the minimum viable version of the design in `docs/design.md` for:

- Page-level CoW on first write using `userfaultfd` write-protect mode
- Persistent undo log for uncommitted page changes
- Crash recovery that restores `B` from `L` when undo log is non-empty
- `cm_commit()` epoch boundary behavior

This plan intentionally prioritizes correctness and debuggability over performance and feature breadth.

## 2. Scope and Non-Goals

### In Scope (v1)

- Single process writer model
- Single active region per process
- One internal fault-handler thread
- Deterministic fixed VA mapping (already present)
- Crash recovery for page data using undo log
- TDD-first implementation of undo, commit, and recovery protocol
- Correctness-first crash/consistency integration tests for undo/commit/recovery

### Out of Scope (v1)

- Multi-process coordination / ownership locks
- Multi-writer or concurrent commit/recovery support
- Performance optimizations (batching, hole punching, lock-free paths)
- General allocator and named-registry implementation beyond what undo/commit/recovery needs
- Full named allocation lifecycle recovery (tracked separately)

## 3. Design Decisions (Keep It Simple)

### 3.1 Layout

Keep the current file layout unchanged:

```
[ Base pages B: size S ]
[ Log pages  L: size S ]
[ Metadata page: size P ]
```

### 3.2 Undo Log Storage

Store undo page indices in the metadata page directly after `cm_metadata_header`.

- Entry type: `uint16_t` page index
- `undo_log_capacity` = logical page count
- Reject `cm_open` if capacity cannot fit in one metadata page
- Reject oversize `opts.logical_size` with hard failure (`CM_ERR_INVALID_ARGUMENT`)

Reason: avoids changing mapping/layout math now, keeps recovery logic straightforward, and supports current test sizes.

### 3.3 CoW Mechanism

Use `userfaultfd` write-protect mode for first-write interception on `B`:

1. Copy `B[i] -> L[i]`
2. Append `i` to undo log
3. Clear WP for page `i` so write proceeds

No fallback mode in v1:

- If `userfaultfd` setup/registration/WP arming fails, return hard error from `cm_open`/`cm_commit` paths

### 3.4 Recovery Order

Replay undo log from the tail:

- `idx = entries[length - 1]`
- copy `L[idx] -> B[idx]`
- atomically decrement `undo_log_length`

This gives natural crash-idempotence: each completed page replay is persisted by shrinking length.

Atomicity/corruption policy:

- `undo_log_length` updates must use naturally aligned atomic stores
- metadata validation on open must reject invalid length/capacity (`CM_ERR_CORRUPT_METADATA`)
- if crash occurs after copy but before pop, restart safely replays the same entry

## 4. Module-by-Module Implementation

### 4.1 `src/metadata.c` and `src/internal/metadata.h`

- Add metadata helpers for undo entries:
  - pointer to undo entry array
  - max entries that fit in metadata page
- During init:
  - compute logical page count
  - verify it fits
  - initialize `undo_log_capacity` and zero `undo_log_length`
- During validate:
  - verify capacity/length bounds
  - verify entry range for `undo_log_length` entries (`entry < page_count`)

### 4.2 `src/undo_log.c`

- Implement:
  - `cm_undo_log_append(size_t page_index)`
  - `cm_undo_log_replay(void)`
  - `cm_undo_log_reset(void)`
- Requirements:
  - bounds checking
  - append fails on overflow
  - replay pops entries one-by-one from tail
  - reset sets length to zero

### 4.3 `src/fault_handler.c` and `src/internal/faults.h`

- Implement `userfaultfd` setup/teardown and handler thread:
  - create uffd (`syscall(SYS_userfaultfd, ...)`)
  - `UFFDIO_API`
  - register `B` with WP mode
  - write-protect all pages at epoch start
- Implement first-write path:
  - compute page index from fault address
  - call `cm_faults_handle_first_write(page_index)`
- `cm_faults_handle_first_write`:
  - copy `B -> L`
  - append undo entry
  - clear WP for that page
- if any step fails, return hard error (no soft fallback to `mprotect` strategy in this milestone)

### 4.4 `src/commit.c`

- Implement `cm_commit_internal()` as:
  1. reset undo log length
  2. write-protect all `B` pages for next epoch
  3. return `CM_OK`

Note: single-writer assumption is accepted in v1.

### 4.5 `src/recovery.c`

- Implement `cm_recover_if_needed()`:
  - if undo log empty: `CM_OK`
  - otherwise replay until length zero
  - return first hard error encountered

### 4.6 `src/cm_api.c`

- Keep flow:
  - `open -> metadata init/validate -> recovery`
- Add fault arming after successful recovery
- Ensure `cm_close()` disarms faults before unmap/unlink

### 4.7 Build System (`CMakeLists.txt`)

- Link `cm_core` with pthreads for fault handler thread
- Keep Linux-only userfaultfd implementation in current target scope

## 5. Crash Injection Hooks (for TDD)

Add deterministic crash/checkpoint points using one unified env var (for tests only): `CM_CRASH_POINT`.

Planned hook names:

- `after_undo_copy_before_append`
- `after_undo_append_before_unprotect`
- `during_commit_after_reset_before_reprotect`
- `during_recovery_after_copy_before_pop`

Implementation style:

- Small helper in `trace.c` or dedicated internal helper
- On match: `abort()`
- Replace existing ad-hoc checkpoint env usage with this same variable for consistency

## 6. Correctness Test Additions (New Files)

Add shared helpers:

- `tests/integration/test_helpers.h`
- `tests/integration/test_helpers.c`

Add new integration correctness tests:

- `tests/integration/test_commit_persists.c`
- `tests/integration/test_uncommitted_rolls_back.c`
- `tests/integration/test_multi_page_cow.c`
- `tests/integration/test_recovery_idempotence.c`

Common helper responsibilities to reduce duplication:

- shared `cm_open`/`cm_close` and shm cleanup helpers
- deterministic page-pattern write/verify helpers
- crash-child launch/assert helpers using `CM_CRASH_POINT`
- metadata read helpers for undo log assertions
## 7. TDD Plan (Phased)

### Phase 1: Metadata + Undo Log Core (No userfaultfd yet)

### Red tests

- `cm_unit_metadata_layout_test`
- `cm_unit_alignment_layout_test`
- `cm_unit_undo_log_integrity_test` (replace TODO with real assertions)
- add `cm_unit_open_undo_capacity_test` (new): oversize logical region must fail cleanly

### Green implementation

- metadata helper/accessor logic
- undo capacity fit check
- undo append/reset/replay core

### Exit criteria

- all above pass reliably

### Phase 2: Recovery Logic

### Red tests

- `cm_unit_undo_log_integrity_test` recovery assertions
- `cm_integration_crash_recovery_test`
- `cm_unit_metadata_corruption_test`

### Green implementation

- `cm_recover_if_needed()`
- `cm_undo_log_replay()` pop-and-copy semantics
- atomic pop ordering (`copy -> atomic length--`)

### Exit criteria

- replay idempotence validated for partial progress scenarios

### Phase 3: userfaultfd First-Write CoW

### Red tests

- add `cm_integration_first_write_cow_test` (new):
  - write a byte in `B`
  - verify exactly one undo entry appended
  - verify `L[i]` contains pre-write bytes
- `cm_unit_commit_test` should still fail at this stage if commit not complete

### Green implementation

- uffd setup/register/wp
- fault thread dispatch
- first-write handler path
- hard-fail behavior when uffd setup/arming fails

### Exit criteria

- first write faults once per page per epoch
- repeated writes in same epoch do not append duplicate entries

### Phase 4: Commit/Reprotect Epoch Boundary

### Red tests

- `cm_unit_commit_test`
- `cm_integration_checkpoint_flow_test`
- update `cm_integration_crash_matrix_test` from TODO to real matrix execution

### Green implementation

- `cm_commit_internal()` reset + re-protect
- crash hook points in commit boundary

### Exit criteria

- post-commit writes re-trigger first-write CoW path
- crash matrix passes for commit boundary points

### Phase 5: End-to-End Crash Matrix

### Red tests

- `cm_integration_crash_matrix_test` full implementation:
  - crash after copy before append
  - crash after append before unprotect
  - crash during commit
  - crash during recovery

### Green implementation

- tighten ordering and error handling where failures appear

### Exit criteria

- consistent restart state across all matrix points

### Phase 6: Data-Consistency Correctness Tests

### Red tests

- `cm_integration_commit_persists_test` (new)
- `cm_integration_uncommitted_rolls_back_test` (new)
- `cm_integration_multi_page_cow_test` (new)
- `cm_integration_recovery_idempotence_test` (new)

### Green implementation

- finalize undo/recovery ordering details exposed by multi-page and crash-during-recovery scenarios
- unify crash hooks in library/tests under `CM_CRASH_POINT`

### Exit criteria

- all four data-consistency tests pass
- no regressions in existing undo/commit/recovery integration tests
## 8. Test Execution Workflow

From `libs/shared_memory_allocator`:

```bash
cmake --preset dev-debug
cmake --build --preset dev-debug
ctest --preset dev-debug -R cm_unit --output-on-failure
ctest --preset dev-debug -R cm_integration --output-on-failure
```

For iterative TDD in each phase, run the narrowest regex matching only the phase tests.

Gating rule for this milestone:

- Do not move to the next phase until all currently failing undo/commit/recovery tests are green, plus newly added correctness tests.
- Out-of-scope allocator/named tests are tracked separately and are not blockers for this milestone.
## 9. Acceptance Criteria

Implementation is complete for this plan when:

- first-write CoW works via `userfaultfd` WP
- undo log records dirtied pages and is bounded/validated
- recovery replays to a consistent committed baseline
- commit starts a clean epoch with full re-protection
- crash-point integration tests pass for undo/commit/recovery boundaries
- new data-consistency tests pass (commit persistence, rollback, multi-page CoW, recovery idempotence)
## 10. Follow-Up Work (After v1)

- Expand metadata beyond one page to remove v1 undo capacity cap
- Add explicit multi-process ownership/locking policy
- Add hole-punch optimization for `L`
- Add full named allocation recovery protocol tests and implementation
