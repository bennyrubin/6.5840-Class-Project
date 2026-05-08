# Transparent Page-Level CoW SHM

## 1. High-Level Goal

We want to maintain a large shared memory region that:

- Can be modified arbitrarily using raw C/C++ pointers
- Maintains commit semantics (`cm_commit()` marks safe restart points)
- Is crash-consistent after process kill
- Does not require data-structure awareness
- Does not require copying the entire region per commit
- Keeps virtual addresses stable at all times

Core invariant:

- `B` (base pages) always represents the last committed state
- `L` (log pages) stores undo copies for pages dirtied since last commit

## 2. Failure Model

We assume:

- Process may be killed at any time
- System does not lose power (tmpfs is not durable across reboot)
- SHM object still exists when process restarts after crash/kill
- If application calls `cm_close()`, the shm object is unlinked and considered finalized

Out of scope:

- Multi-writer or concurrent commit/recovery coordination

## 3. Interface

`cm_open(shm, opts)`

- Create/open shm object
- Map at fixed VA
- Recover to a consistent state if log is non-empty
- Only one region is active per process in this version

`cm_close()`

- Release process-local resources (mappings, file descriptors, runtime state)
- Unlink backing shm object to indicate computation is done and memory is no longer needed
- After `cm_close()`, reopening the same name starts from a fresh object

`cm_get_oralloc(name, size, init_fn)`

- Return existing named allocation if present
- Otherwise allocate and run one-time initialization protocol

`cm_alloc(size)`

- Allocate unnamed bytes from allocator arena

`cm_commit()`

- Called when application state is consistent
- Clears undo-log metadata and starts a new write-protected epoch

## 4. Mapping and Layout

### 4.1 Logical Region

Fixed virtual range:

`LOGICAL = [BASE_VA, BASE_VA + S)`

All application pointers target this range.

### 4.2 SHM File Layout

```
[ Base pages B: size S ]
[ Log pages L:  size S ]
[ Metadata region ]
```

Each logical page index `i` has:

- `B[i]`: committed data
- `L[i]`: undo image (captured before first write in current epoch)

### 4.3 Metadata Layout (Implementation Section)

Metadata must include:

- Header: magic, version, page size, region sizes
- Undo page index log (append-only within an epoch), plus length/capacity metadata
- Allocator header: arena current offset + arena limit
- Undo-aware control slab in `B` (reserved tail page) containing:
  - Named allocation registry
  - Named allocation entries/state (`allocating`, `initializing`, `ready`)

Alignment requirements:

- All page-backed regions (`B`, `L`) are page-aligned
- Metadata structures are at least 8-byte aligned
- Persistent structs used with atomics are naturally aligned for their width

Why alignment matters:

- Page alignment is required for `mprotect`, page indexing, and fault handling correctness
- Misaligned atomic fields can tear or fault on some architectures
- Stable alignment ensures deterministic layout across restarts and versions

Control slab note:

- The last page of `B` is reserved for allocator/registry metadata.
- Allocator user bytes are limited to `[0, arena_limit)`, where `arena_limit = logical_size - page_size`.
- Because this slab lives in `B`, its updates automatically follow undo/commit/recovery semantics.

## 5. Consistency Scheme (Undo Log)

No explicit commit phases or generation counters are used. Correctness relies on strict ordering and invariants.

### 5.1 Epoch Start

- Entire `B` region is write-protected (via `userfaultfd` WP or `mprotect` strategy)
- Undo log starts empty for the epoch

### 5.2 First Write to Page `i`

On first write fault for page `i`:

1. Copy `B[i] -> L[i]` completely
2. Append page index `i` to the undo log
3. Remove write protection for `B[i]` so application write proceeds

If page `i` is written again in same epoch, no additional log copy is needed.

### 5.3 Commit (`cm_commit()`)

`cm_commit()` means: "current bytes in `B` become the restart baseline."

Required order:

1. Ensure all application writes in `B` are visible
2. Reset undo-log length to empty
3. Re-protect `B` pages for next epoch
4. Optionally hole-punch `L` pages corresponding to logged page indices from prior epoch

After step 2, restart sees an empty log and treats `B` as committed.

### 5.4 Recovery on `cm_open()`

If undo log is non-empty, run recovery:

1. For each page index `i` recorded in undo log, copy `L[i] -> B[i]`
2. Remove consumed entries from the log (or equivalently shrink from the front)
3. Continue until undo log length is zero

This is idempotent: crashing mid-recovery is safe, because replay resumes from remaining log entries.

## 6. Fixed Virtual Address Strategy

Current requirement: map at a fixed virtual address using `MAP_FIXED_NOREPLACE`; fail fast if address is unavailable.

Plan to reduce conflicts:

1. Reserve the full target VA range at program start (before other large mappings)
2. Choose a high, sparse canonical range and keep it configurable
3. Probe availability early during process bootstrap
4. If unavailable, terminate with a clear diagnostic (do not relocate silently)

Note: Disabling ASLR may be used in development, but production behavior should rely on deterministic early reservation and strict failure if mapping cannot be honored.

## 7. Concurrency Model

For now, concurrency is out of scope:

- Assume single process writer
- Assume single thread handles faults/commit path
- No concurrent commits or concurrent recovery

This simplifies ordering guarantees for the initial version.

## 8. Allocator and Named Initialization

## 8.1 Arena Allocator

- Baseline allocator is bump-pointer in `LOGICAL` plus a simple persistent free list
- Freed blocks are tracked in a singly linked free list using in-arena block headers
- Initial implementation does not split or coalesce blocks

## 8.2 Named Registry

Persistent registry maps:

- `name -> offset`
- size
- optional type/version metadata

## 8.3 Named Object Lifecycle

Each named object has a persistent state:

- `allocating`: slot reserved, memory range assigned, object not yet initialized
- `initializing`: `init_fn` started but not completed
- `ready`: fully initialized and safe to return on restart

`cm_get_oralloc(name, size, init_fn)` flow:

1. Lookup `name`
2. If `ready`, return pointer
3. If absent, create entry as `allocating`, reserve space
4. Transition to `initializing`, run `init_fn`
5. On success, mark `ready`

Crash handling:

- If restart sees `allocating` or `initializing`, treat as incomplete and re-run safe initialization path (or reset and recreate entry based on policy)
- `ready` is the only state exposed as valid object

## 9. Testing Plan

### 9.1 Example Apps

- Minimal arithmetic state machine app validating interface calls
- K-means workload to stress page writes and restart behavior

### 9.2 Unit Tests

- `cm_open`, `cm_close`, `cm_alloc`, `cm_get_oralloc`, `cm_commit`
- `cm_close()` unlink semantics (shm name no longer openable after close)
- SHM creation/open behavior and metadata initialization
- Allocator alignment, bounds, and exhaustion behavior
- Named lifecycle transitions: `allocating -> initializing -> ready`

### 9.3 Crash-Point Integration Tests

Inject process kill at boundaries:

- After `B[i] -> L[i]` copy, before append-to-undo-log
- After append-to-undo-log, before unprotect
- During `cm_commit()` while clearing log entries
- During recovery while replaying entries
- During named object initialization before `ready`

Validate:

- Restart is consistent
- Recovery is idempotent
- No partially initialized named object is exposed as `ready`

### 9.4 Multi-Page Correctness Tests

- Modify many pages, commit, restart, verify checksum/model result
- Crash before commit and verify rollback via recovery
- Crash after commit and verify new state persists
