# Shared Memory Allocator

Scaffold for a page-level CoW shared-memory allocator/runtime.

Current status:

- C API and internal module layout are in place.
- Public API includes `cm_open`, `cm_close`, `cm_alloc`, `cm_free`, `cm_realloc`, `cm_get_oralloc`, and `cm_commit`.
- API is process-global (single active region per process), so no user-visible handle is required.
- Inspection API scaffold is available via `cm_inspect_open/read_layout/dump/close`.
- Implementations are mostly stubs (`CM_ERR_NOT_IMPLEMENTED`).
- Unit and integration tests are wired into CMake and currently fail by design.
- `cm_close` semantics are destructive in this project: it unlinks the backing shm object.

## Build with CMake

From repo root:

```bash
cmake -S libs/shared_memory_allocator -B libs/shared_memory_allocator/build
cmake --build libs/shared_memory_allocator/build -j4
```

What these do:

- `cmake -S ... -B ...`: Configure the project.
- `-S`: Source directory containing `CMakeLists.txt`.
- `-B`: Build directory where generated build files are written.
- `cmake --build ...`: Compile all targets in that build directory.
- `-j4`: Build with up to 4 parallel jobs.

Shorter preset-based flow (from `libs/shared_memory_allocator`):

```bash
cmake --preset dev-debug
cmake --build --preset dev-debug
ctest --preset dev-debug
```

Release-like preset:

```bash
cmake --preset dev-release
cmake --build --preset dev-release
ctest --preset dev-release
```

## Run Tests

After building:

```bash
ctest --test-dir libs/shared_memory_allocator/build --output-on-failure
```

Notes:

- `ctest` runs tests registered via `add_test(...)` in CMake.
- `--output-on-failure` prints stderr/stdout for failing tests.
- Right now tests are expected to fail until stubs are implemented.

Run only unit tests:

```bash
ctest --test-dir libs/shared_memory_allocator/build -R cm_unit --output-on-failure
```

Run only integration tests:

```bash
ctest --test-dir libs/shared_memory_allocator/build -R cm_integration --output-on-failure
```

## Run Example Applications

Build first, then run executables from build dir:

```bash
./libs/shared_memory_allocator/build/cm_example_minimal
./libs/shared_memory_allocator/build/cm_example_named
```

Expected currently:

- They compile and run.
- They likely exit non-zero due to `CM_ERR_NOT_IMPLEMENTED`.

## Run Integration Helper Apps

```bash
./libs/shared_memory_allocator/build/cm_integration_simple_writer
./libs/shared_memory_allocator/build/cm_integration_simple_verify
```

Checkpoint integration test executable (optional direct run):

```bash
CHECKPOINT=after_open ./libs/shared_memory_allocator/build/cm_integration_checkpoint_flow_test
```

## Run SHM Dump Tool

```bash
./libs/shared_memory_allocator/build/dev-debug/cm_dump_shm /cm_example_minimal
```

Current expected behavior:

- Tool invokes the inspection API scaffold.
- It exits with non-zero until implementation is completed.

With presets, example binaries are under:

- `libs/shared_memory_allocator/build/dev-debug/`
- `libs/shared_memory_allocator/build/dev-release/`

## Debug Mode Macros

`cm_core` gets `CM_DEBUG=1` automatically in `Debug` builds via CMake.

Use macros from `src/internal/debug.h`:

- `CM_DASSERT(expr)` for debug-only assertions
- `CM_DLOG(component, event, detail)` for debug-only trace logs

No per-call `#ifdef` is needed; macros compile to no-ops outside debug.

Enable runtime trace output with:

```bash
CM_TRACE=1 ./libs/shared_memory_allocator/build/dev-debug/cm_example_minimal
```

## Layout

```text
libs/shared_memory_allocator/
├── CMakeLists.txt
├── README.md
├── docs/
│   └── design.md
├── include/
│   ├── cm/
│   │   ├── cm.h
│   │   ├── cm_inspect.h
│   │   ├── cm_types.h
│   │   └── cm_version.h
├── src/
│   ├── cm_api.c
│   ├── inspect.c
│   ├── mapping.c
│   ├── metadata.c
│   ├── undo_log.c
│   ├── fault_handler.c
│   ├── commit.c
│   ├── recovery.c
│   ├── allocator.c
│   ├── trace.c
│   ├── layout.c
│   └── internal/
│       ├── allocator.h
│       ├── commit.h
│       ├── faults.h
│       ├── layout.h
│       ├── mapping.h
│       ├── metadata.h
│       ├── recovery.h
│       ├── trace.h
│       └── undo_log.h
├── examples/
│   ├── minimal_usage.c
│   └── named_allocation_usage.c
└── tests/
    ├── unit/
    │   ├── test_cm_alloc.c
    │   ├── test_cm_close.c
    │   ├── test_cm_close_unlinks.c
    │   ├── test_cm_commit.c
    │   ├── test_cm_named.c
    │   └── test_cm_open.c
    └── integration/
        ├── apps/
        │   ├── simple_verify_app.c
        │   └── simple_writer_app.c
        ├── test_checkpoint_flow.c
        └── test_crash_recovery.c
```
