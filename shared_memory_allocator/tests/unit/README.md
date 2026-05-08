# Unit Tests

These unit tests target the public C API surface, metadata/layout invariants, and undo-log correctness.

## Test binaries

- `cm_unit_open_test` (`test_cm_open.c`)
  - Validates that `cm_open(...)` succeeds with basic options.

- `cm_unit_close_test` (`test_cm_close.c`)
  - Validates that `cm_open(...)` succeeds and `cm_close(...)` can be called safely.

- `cm_unit_close_unlinks_test` (`test_cm_close_unlinks.c`)
  - Validates destructive close semantics: `cm_close(...)` should unlink the backing shm object.

- `cm_unit_alloc_test` (`test_cm_alloc.c`)
  - Validates that `cm_open(...)` succeeds and `cm_alloc(...)` returns non-NULL for a small allocation.

- `cm_unit_free_test` (`test_cm_free.c`)
  - Validates `cm_free(...)` and simple free-list reuse behavior.

- `cm_unit_realloc_test` (`test_cm_realloc.c`)
  - Validates grow/shrink `cm_realloc(...)` semantics and data preservation.

- `cm_unit_realloc_edge_cases_test` (`test_cm_realloc_edge_cases.c`)
  - Validates `cm_realloc(...)` edge cases (`NULL`, size 0, invalid pointer).

- `cm_unit_named_test` (`test_cm_named.c`)
  - Validates named allocation path `cm_get_oralloc(...)` with an init callback.

- `cm_unit_commit_test` (`test_cm_commit.c`)
  - Validates that `cm_commit(...)` succeeds after `cm_open(...)`.

- `cm_unit_open_undo_capacity_test` (`test_cm_open_undo_capacity.c`)
  - Verifies oversize logical regions are rejected when undo entries cannot fit metadata constraints.

- `cm_unit_metadata_layout_test` (`test_metadata_layout.c`)
  - Validates metadata header fields and persisted layout sizing.

- `cm_unit_alignment_layout_test` (`test_alignment_layout.c`)
  - Validates page and metadata alignment guarantees.

- `cm_unit_undo_log_integrity_test` (`test_undo_log_integrity.c`)
  - Validates undo-log append/replay/reset invariants and bounds behavior.

- `cm_unit_metadata_corruption_test` (`test_metadata_corruption.c`)
  - Validates corruption-path handling (invalid metadata rejected on open).

## Run

From `libs/shared_memory_allocator`:

```bash
ctest --preset dev-debug -R cm_unit --output-on-failure
```
