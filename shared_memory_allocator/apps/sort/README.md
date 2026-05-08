# Sort App (C)

This app sorts integer datasets using an indirect permutation array.

Modes:

- `normal`: plain process memory.
- `cm`: shared-memory allocator mode (`cm_open`, `cm_get_oralloc`, `cm_commit`).

Algorithms:

- `merge`: iterative bottom-up merge sort on `perm[]`.
- `heap`: in-place heapsort on `perm[]`.

Data model:

- `keys[]` contains integer values loaded from `data/*.txt`.
- `perm[]` starts as identity and gets sorted by `keys[perm[i]]`.
- Verification checks `perm[]` is a full permutation and keys are non-decreasing.

## Generate data

```bash
python3 apps/sort/data/generate_data.py \
  --output apps/sort/data/ids.csv \
  --count 20000 \
  --seed 42
```

## Run

```bash
./build/dev-debug/cm_app_sort --mode normal --algo merge
./build/dev-debug/cm_app_sort --mode normal --algo heap
./build/dev-debug/cm_app_sort --mode cm --algo merge --shm-name /cm_sort_merge --commit-every 32
./build/dev-debug/cm_app_sort --mode cm --algo heap --shm-name /cm_sort_heap --commit-every 32
```

Default data path is compiled as `apps/sort/data/ids.csv`.

`--commit-every 0` disables periodic commits.
