# K-Means App (C)

This app provides two K-Means implementations with the same algorithmic core:

- `kmeans(...)`: normal in-process implementation.
- `cm_kmeans(...)`: crash-resumable implementation using `cm` named allocations.

## Files

- `kmeans.c` / `kmeans.h`: loader + algorithm + CM integration.
- `main.c`: CLI entrypoint that selects mode with `--mode normal|cm`.
- `data/generate_data.py`: dataset generator.
- `data/points.csv`: sample dataset (`x,y` CSV).

## Difference Between `kmeans` and `cm_kmeans`

Both implementations call the same core routine `km_run_core(...)` for the Lloyd loop
(assignment/update/inertia logic). The algorithm itself is intentionally the same.

The only persistent-state difference is in `cm_kmeans(...)`:

1. `cm_open(...)` is called at the beginning.
1. Two named objects are fetched/created with `cm_get_oralloc(...)`:
   - `"kmeans_centers"`: `k * sizeof(km_point)`
   - `"kmeans_iteration"`: `sizeof(int)`
1. Those pointers are passed into `km_run_core(...)`.
1. `cm_close()` is called at the end.

No other K-Means working data is persisted in CM. Labels, sums, counts, etc. are recomputed.

## Where `cm_commit()` Happens

`cm_commit()` is called once per completed iteration via the CM commit callback:

- `km_commit_with_cm(...)` wraps `cm_commit()`.
- `km_run_core(...)` invokes that callback **after**:
  - assignment/update work for that iteration is complete,
  - centers are updated,
  - `iteration_ptr` has been incremented.
- The callback is executed before the loop checks for convergence and exits.

This means each persisted snapshot represents a fully completed iteration with matching:

- `kmeans_centers`
- `kmeans_iteration`

## CLI

`main.c` supports:

- `--data PATH`
- `--k N`
- `--max-iter N`
- `--mode normal|cm`
- `--shm-name /cm_name` (used only in `cm` mode)

Example:

```bash
./build/dev-debug/cm_app_kmeans --mode normal --k 4 --max-iter 200
./build/dev-debug/cm_app_kmeans --mode cm --k 4 --max-iter 200 --shm-name /cm_kmeans_app
```
