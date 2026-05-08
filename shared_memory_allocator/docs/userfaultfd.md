# userfaultfd and Fault Interception in This Runtime

## Why this exists

The allocator runtime needs page-first-write interception so it can:

1. Copy `B[i] -> L[i]` (undo image),
2. Append page index `i` to the undo log,
3. Allow the write to proceed.

This is the core mechanism behind crash-consistent undo logging.

This now also covers allocator/registry metadata, because those structures live in a reserved tail page inside `B`.

## What `userfaultfd` is

`userfaultfd` is a Linux kernel interface that delivers page-fault events to user space, so a user-space handler can resolve them.

High-level primitives:

- `userfaultfd(...)` syscall: create the fault event file descriptor.
- `UFFDIO_API`: negotiate API version and features.
- `UFFDIO_REGISTER`: register an address range for fault tracking modes.
- `UFFDIO_COPY` / `UFFDIO_CONTINUE`: resolve faults.
- `UFFDIO_WRITEPROTECT`: toggle write protection and get WP faults.

Typical CoW design for this project would use:

- `UFFDIO_REGISTER_MODE_WP` on base range `B`,
- first write fault per page to run undo-log logic.

## Current implementation status in this repo

The implementation point is `src/fault_handler.c`.

In this environment, shared-memory WP registration is not available in a way that supports this v1 protocol end-to-end (kernel accepts `userfaultfd` setup, but WP registration/write-protect semantics needed for shmem CoW are not usable for this mapping layout).

Because of that, the runtime currently uses the design doc's alternate fault strategy:

- `mprotect(PROT_READ)` over `B` to start an epoch,
- `SIGSEGV` handler intercepts first write on each page,
- handler calls `cm_faults_handle_first_write(page_index)`,
- page is switched to `PROT_READ|PROT_WRITE` after undo append.

This preserves the same protocol ordering and crash hooks used by undo/recovery tests.

It also means writes to the reserved allocator/registry control page in `B` go through the same first-write logging flow.

## Where fault handling is wired

- `src/cm_api.c`
  - `cm_open(...)`: mapping/metadata/recovery, then `cm_faults_arm()`
  - `cm_close(...)`: `cm_faults_disarm()` before unmap/unlink
- `src/commit.c`
  - `cm_commit_internal()`: reset undo log, then re-arm faults (new epoch)
- `src/fault_handler.c`
  - `cm_faults_arm()`: installs SIGSEGV handler and protects `B`
  - `cm_faults_handle_first_write()`: copy/append/unprotect order
  - `cm_faults_disarm()`: restore RW mapping and previous SIGSEGV action

## Crash points used in tests

Crash points are controlled by `CM_CRASH_POINT`:

- `after_undo_copy_before_append`
- `after_undo_append_before_unprotect`
- `during_commit_after_reset_before_reprotect`
- `during_recovery_after_copy_before_pop`

These are triggered via `cm_trace_maybe_abort(...)` in:

- `src/fault_handler.c`
- `src/commit.c`
- `src/undo_log.c`

## If you want to re-enable a full `userfaultfd` WP path

Primary place to rework is `src/fault_handler.c`:

1. Add a dedicated fault thread polling `uffd`.
2. Register base range with usable WP mode for the target kernel/mapping.
3. Route WP events to `cm_faults_handle_first_write(...)`.
4. Keep same ordering guarantees and crash hooks.

The rest of the runtime (`undo_log`, `recovery`, `commit`, metadata validation) is already structured around this protocol and does not depend on signal-vs-uffd transport choice.
