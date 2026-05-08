#define _POSIX_C_SOURCE 200809L

#include "sort.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cm/cm.h"

#define SM_SORT_MERGE_STATE_MAGIC 0x4d45524745535431ULL
#define SM_SORT_HEAP_STATE_MAGIC 0x4845415053545431ULL

typedef sm_sort_status (*sm_sort_commit_fn)(void* ctx);

typedef struct sm_sort_merge_state {
  uint64_t magic;
  size_t n;
  size_t width;
  size_t left;
  size_t steps_done;
  int done;
} sm_sort_merge_state;

typedef struct sm_sort_heap_state {
  uint64_t magic;
  size_t n;
  int phase;
  size_t build_next;
  size_t extract_next;
  size_t steps_done;
  int done;
} sm_sort_heap_state;

typedef struct sm_sort_commit_policy {
  int enabled;
  size_t every;
  size_t op_count;
  size_t committed_op_count;
  size_t commit_count;
  sm_sort_commit_fn commit_fn;
  void* commit_ctx;
} sm_sort_commit_policy;

typedef struct sm_sort_keys_init_ctx {
  const char* path;
  size_t n_values;
} sm_sort_keys_init_ctx;

typedef struct sm_sort_perm_init_ctx {
  size_t n_values;
} sm_sort_perm_init_ctx;

static size_t sm_sort_min_size(size_t a, size_t b) {
  return a < b ? a : b;
}

static size_t sm_sort_align_up(size_t value, size_t alignment) {
  size_t remainder;
  if (alignment == 0) {
    return value;
  }
  remainder = value % alignment;
  if (remainder == 0) {
    return value;
  }
  return value + (alignment - remainder);
}

/*
 * Returns:
 *  1 => parsed integer in out_value
 *  0 => line is empty/comment and should be ignored
 * -1 => invalid syntax
 */
static int sm_sort_parse_i64_line(const char* line, int64_t* out_value) {
  char* end = NULL;
  long long parsed;

  if (line == NULL || out_value == NULL) {
    return -1;
  }

  while (*line != '\0' && isspace((unsigned char)*line)) {
    ++line;
  }
  if (*line == '\0' || *line == '#') {
    return 0;
  }

  errno = 0;
  parsed = strtoll(line, &end, 10);
  if (errno != 0 || end == line) {
    return -1;
  }

  while (*end != '\0' && isspace((unsigned char)*end)) {
    ++end;
  }
  if (*end != '\0') {
    return -1;
  }

  *out_value = (int64_t)parsed;
  return 1;
}

static sm_sort_status sm_sort_count_values(const char* path, size_t* out_count) {
  FILE* fp;
  char* line = NULL;
  size_t line_cap = 0;
  ssize_t line_len;
  size_t count = 0;

  if (path == NULL || out_count == NULL) {
    return SM_SORT_ERR_INVALID_ARGUMENT;
  }

  fp = fopen(path, "r");
  if (fp == NULL) {
    return SM_SORT_ERR_IO;
  }

  while ((line_len = getline(&line, &line_cap, fp)) != -1) {
    int64_t value;
    int parsed;
    (void)line_len;

    parsed = sm_sort_parse_i64_line(line, &value);
    if (parsed < 0) {
      free(line);
      fclose(fp);
      return SM_SORT_ERR_PARSE;
    }
    if (parsed == 1) {
      ++count;
    }
  }

  free(line);
  fclose(fp);
  if (count == 0) {
    return SM_SORT_ERR_PARSE;
  }

  *out_count = count;
  return SM_SORT_OK;
}

static sm_sort_status sm_sort_fill_values_from_file(
    const char* path,
    int64_t* out_values,
    size_t n_values) {
  FILE* fp;
  char* line = NULL;
  size_t line_cap = 0;
  ssize_t line_len;
  size_t index = 0;

  if (path == NULL || out_values == NULL || n_values == 0) {
    return SM_SORT_ERR_INVALID_ARGUMENT;
  }

  fp = fopen(path, "r");
  if (fp == NULL) {
    return SM_SORT_ERR_IO;
  }

  while ((line_len = getline(&line, &line_cap, fp)) != -1) {
    int64_t value;
    int parsed;
    (void)line_len;

    parsed = sm_sort_parse_i64_line(line, &value);
    if (parsed < 0) {
      free(line);
      fclose(fp);
      return SM_SORT_ERR_PARSE;
    }
    if (parsed == 0) {
      continue;
    }
    if (index >= n_values) {
      free(line);
      fclose(fp);
      return SM_SORT_ERR_PARSE;
    }

    out_values[index++] = value;
  }

  free(line);
  fclose(fp);

  if (index != n_values) {
    return SM_SORT_ERR_PARSE;
  }
  return SM_SORT_OK;
}

static sm_sort_status sm_sort_load_values_malloc(
    const char* path,
    int64_t** out_values,
    size_t* out_count) {
  sm_sort_status status;
  size_t count;
  int64_t* values;

  if (path == NULL || out_values == NULL || out_count == NULL) {
    return SM_SORT_ERR_INVALID_ARGUMENT;
  }

  status = sm_sort_count_values(path, &count);
  if (status != SM_SORT_OK) {
    return status;
  }

  values = (int64_t*)malloc(count * sizeof(*values));
  if (values == NULL) {
    return SM_SORT_ERR_ALLOC;
  }

  status = sm_sort_fill_values_from_file(path, values, count);
  if (status != SM_SORT_OK) {
    free(values);
    return status;
  }

  *out_values = values;
  *out_count = count;
  return SM_SORT_OK;
}

static void sm_sort_init_identity_perm(size_t* perm, size_t n) {
  size_t i;
  for (i = 0; i < n; ++i) {
    perm[i] = i;
  }
}

static void sm_sort_merge_state_reset(sm_sort_merge_state* state, size_t n) {
  state->magic = SM_SORT_MERGE_STATE_MAGIC;
  state->n = n;
  state->width = 1;
  state->left = 0;
  state->steps_done = 0;
  state->done = (n < 2) ? 1 : 0;
}

static void sm_sort_heap_state_reset(sm_sort_heap_state* state, size_t n) {
  state->magic = SM_SORT_HEAP_STATE_MAGIC;
  state->n = n;
  state->phase = (n < 2) ? 2 : 0;
  state->build_next = n / 2;
  state->extract_next = (n == 0) ? 0 : (n - 1);
  state->steps_done = 0;
  state->done = (n < 2) ? 1 : 0;
}

static sm_sort_status sm_sort_commit_cm(void* ctx) {
  cm_status status;

  (void)ctx;
  status = cm_commit();
  return cm_status_is_error(status) ? SM_SORT_ERR_CM : SM_SORT_OK;
}

static void sm_sort_policy_record_step(sm_sort_commit_policy* policy) {
  if (policy == NULL) {
    return;
  }
  policy->op_count += 1;
}

static sm_sort_status sm_sort_policy_maybe_commit(sm_sort_commit_policy* policy, int force) {
  if (policy == NULL || !policy->enabled) {
    return SM_SORT_OK;
  }
  if (policy->commit_fn == NULL || policy->every == 0) {
    return SM_SORT_ERR_INVALID_ARGUMENT;
  }

  if (!force) {
    if (policy->op_count == 0 || (policy->op_count % policy->every) != 0) {
      return SM_SORT_OK;
    }
  }
  if (policy->op_count == policy->committed_op_count) {
    return SM_SORT_OK;
  }

  if (policy->commit_fn(policy->commit_ctx) != SM_SORT_OK) {
    return SM_SORT_ERR_CM;
  }
  policy->committed_op_count = policy->op_count;
  policy->commit_count += 1;
  return SM_SORT_OK;
}

static void sm_sort_merge_range(
    const int64_t* keys,
    size_t* perm,
    size_t* tmp,
    size_t left,
    size_t mid,
    size_t right) {
  size_t i = left;
  size_t j = mid;
  size_t k = left;

  while (i < mid && j < right) {
    if (keys[perm[i]] <= keys[perm[j]]) {
      tmp[k++] = perm[i++];
    } else {
      tmp[k++] = perm[j++];
    }
  }
  while (i < mid) {
    tmp[k++] = perm[i++];
  }
  while (j < right) {
    tmp[k++] = perm[j++];
  }

  for (k = left; k < right; ++k) {
    perm[k] = tmp[k];
  }
}

static sm_sort_status sm_sort_run_merge(
    const int64_t* keys,
    size_t n_values,
    size_t* perm,
    size_t* tmp,
    sm_sort_merge_state* state,
    sm_sort_commit_policy* policy,
    int* out_resumed) {
  sm_sort_status status;
  int resumed = 0;

  if (keys == NULL || perm == NULL || tmp == NULL || state == NULL || out_resumed == NULL) {
    return SM_SORT_ERR_INVALID_ARGUMENT;
  }

  if (state->magic == SM_SORT_MERGE_STATE_MAGIC && state->n == n_values) {
    resumed = (state->steps_done > 0 && !state->done) ? 1 : 0;
  } else {
    sm_sort_merge_state_reset(state, n_values);
  }

  if (state->width == 0) {
    state->width = 1;
  }

  while (!state->done) {
    if (state->width >= n_values) {
      state->done = 1;
      break;
    }

    while (state->left < n_values) {
      size_t left = state->left;
      size_t mid = sm_sort_min_size(left + state->width, n_values);
      size_t right = sm_sort_min_size(left + (state->width * 2), n_values);

      if (mid < right) {
        sm_sort_merge_range(keys, perm, tmp, left, mid, right);
      }

      state->left += state->width * 2;
      state->steps_done += 1;
      sm_sort_policy_record_step(policy);
      status = sm_sort_policy_maybe_commit(policy, 0);
      if (status != SM_SORT_OK) {
        *out_resumed = resumed;
        return status;
      }
    }

    state->left = 0;
    if (state->width > (n_values / 2)) {
      state->width = n_values;
    } else {
      state->width *= 2;
    }
    state->steps_done += 1;
    sm_sort_policy_record_step(policy);
    status = sm_sort_policy_maybe_commit(policy, 0);
    if (status != SM_SORT_OK) {
      *out_resumed = resumed;
      return status;
    }
  }

  status = sm_sort_policy_maybe_commit(policy, 1);
  if (status != SM_SORT_OK) {
    *out_resumed = resumed;
    return status;
  }

  *out_resumed = resumed;
  return SM_SORT_OK;
}

static void sm_sort_heap_sift_down(const int64_t* keys, size_t* perm, size_t root, size_t heap_size) {
  while (1) {
    size_t left = (root * 2) + 1;
    size_t right = left + 1;
    size_t largest = root;

    if (left >= heap_size) {
      return;
    }
    if (keys[perm[left]] > keys[perm[largest]]) {
      largest = left;
    }
    if (right < heap_size && keys[perm[right]] > keys[perm[largest]]) {
      largest = right;
    }
    if (largest == root) {
      return;
    }

    {
      size_t tmp = perm[root];
      perm[root] = perm[largest];
      perm[largest] = tmp;
    }
    root = largest;
  }
}

static sm_sort_status sm_sort_run_heap(
    const int64_t* keys,
    size_t n_values,
    size_t* perm,
    sm_sort_heap_state* state,
    sm_sort_commit_policy* policy,
    int* out_resumed) {
  sm_sort_status status;
  int resumed = 0;

  if (keys == NULL || perm == NULL || state == NULL || out_resumed == NULL) {
    return SM_SORT_ERR_INVALID_ARGUMENT;
  }

  if (state->magic == SM_SORT_HEAP_STATE_MAGIC && state->n == n_values) {
    resumed = (state->steps_done > 0 && !state->done) ? 1 : 0;
  } else {
    sm_sort_heap_state_reset(state, n_values);
  }

  while (!state->done) {
    if (state->phase == 0) {
      while (state->build_next > 0) {
        state->build_next -= 1;
        sm_sort_heap_sift_down(keys, perm, state->build_next, n_values);

        state->steps_done += 1;
        sm_sort_policy_record_step(policy);
        status = sm_sort_policy_maybe_commit(policy, 0);
        if (status != SM_SORT_OK) {
          *out_resumed = resumed;
          return status;
        }
      }
      state->phase = 1;
      state->extract_next = (n_values == 0) ? 0 : (n_values - 1);
      continue;
    }

    if (state->phase == 1) {
      while (state->extract_next > 0) {
        size_t end = state->extract_next;
        size_t tmp = perm[0];
        perm[0] = perm[end];
        perm[end] = tmp;
        sm_sort_heap_sift_down(keys, perm, 0, end);
        state->extract_next -= 1;

        state->steps_done += 1;
        sm_sort_policy_record_step(policy);
        status = sm_sort_policy_maybe_commit(policy, 0);
        if (status != SM_SORT_OK) {
          *out_resumed = resumed;
          return status;
        }
      }

      state->phase = 2;
      state->done = 1;
      break;
    }

    state->phase = 2;
    state->done = 1;
  }

  status = sm_sort_policy_maybe_commit(policy, 1);
  if (status != SM_SORT_OK) {
    *out_resumed = resumed;
    return status;
  }

  *out_resumed = resumed;
  return SM_SORT_OK;
}

static sm_sort_status sm_sort_verify_permutation(
    const int64_t* keys,
    size_t n_values,
    const size_t* perm) {
  uint8_t* seen;
  size_t i;

  if (keys == NULL || perm == NULL) {
    return SM_SORT_ERR_INVALID_ARGUMENT;
  }

  seen = (uint8_t*)calloc(n_values, sizeof(*seen));
  if (seen == NULL) {
    return SM_SORT_ERR_ALLOC;
  }

  for (i = 0; i < n_values; ++i) {
    if (perm[i] >= n_values || seen[perm[i]] != 0) {
      free(seen);
      return SM_SORT_ERR_VERIFY;
    }
    seen[perm[i]] = 1;

    if (i > 0 && keys[perm[i - 1]] > keys[perm[i]]) {
      free(seen);
      return SM_SORT_ERR_VERIFY;
    }
  }

  free(seen);
  return SM_SORT_OK;
}

static sm_sort_status sm_sort_run_normal(const sm_sort_run_opts* opts, sm_sort_summary* out_summary) {
  sm_sort_status status;
  int64_t* keys = NULL;
  size_t* perm = NULL;
  size_t* tmp = NULL;
  sm_sort_merge_state merge_state;
  sm_sort_heap_state heap_state;
  sm_sort_commit_policy policy;
  size_t n_values = 0;
  int resumed = 0;

  memset(&merge_state, 0, sizeof(merge_state));
  memset(&heap_state, 0, sizeof(heap_state));
  memset(&policy, 0, sizeof(policy));

  status = sm_sort_load_values_malloc(opts->data_path, &keys, &n_values);
  if (status != SM_SORT_OK) {
    return status;
  }

  perm = (size_t*)malloc(n_values * sizeof(*perm));
  if (perm == NULL) {
    free(keys);
    return SM_SORT_ERR_ALLOC;
  }
  sm_sort_init_identity_perm(perm, n_values);

  if (opts->algo == SM_SORT_ALGO_MERGE) {
    tmp = (size_t*)malloc(n_values * sizeof(*tmp));
    if (tmp == NULL) {
      free(perm);
      free(keys);
      return SM_SORT_ERR_ALLOC;
    }
    sm_sort_merge_state_reset(&merge_state, n_values);
    status = sm_sort_run_merge(keys, n_values, perm, tmp, &merge_state, &policy, &resumed);
  } else {
    sm_sort_heap_state_reset(&heap_state, n_values);
    status = sm_sort_run_heap(keys, n_values, perm, &heap_state, &policy, &resumed);
  }
  if (status != SM_SORT_OK) {
    free(tmp);
    free(perm);
    free(keys);
    return status;
  }

  status = sm_sort_verify_permutation(keys, n_values, perm);
  if (status != SM_SORT_OK) {
    free(tmp);
    free(perm);
    free(keys);
    return status;
  }

  if (out_summary != NULL) {
    out_summary->n_values = n_values;
    out_summary->n_commits = 0;
    out_summary->resumed = resumed;
  }

  free(tmp);
  free(perm);
  free(keys);
  return SM_SORT_OK;
}

static cm_status sm_sort_init_keys_from_file(void* ptr, size_t size, void* ctx) {
  sm_sort_keys_init_ctx* keys_ctx = (sm_sort_keys_init_ctx*)ctx;
  sm_sort_status status;

  if (ptr == NULL || keys_ctx == NULL) {
    return CM_ERR_INVALID_ARGUMENT;
  }
  if (size != (keys_ctx->n_values * sizeof(int64_t))) {
    return CM_ERR_INVALID_ARGUMENT;
  }

  status = sm_sort_fill_values_from_file(keys_ctx->path, (int64_t*)ptr, keys_ctx->n_values);
  if (status != SM_SORT_OK) {
    return CM_ERR_IO;
  }
  return CM_OK;
}

static cm_status sm_sort_init_perm(void* ptr, size_t size, void* ctx) {
  sm_sort_perm_init_ctx* perm_ctx = (sm_sort_perm_init_ctx*)ctx;

  if (ptr == NULL || perm_ctx == NULL) {
    return CM_ERR_INVALID_ARGUMENT;
  }
  if (size != (perm_ctx->n_values * sizeof(size_t))) {
    return CM_ERR_INVALID_ARGUMENT;
  }

  sm_sort_init_identity_perm((size_t*)ptr, perm_ctx->n_values);
  return CM_OK;
}

static cm_status sm_sort_init_merge_tmp(void* ptr, size_t size, void* ctx) {
  (void)ctx;
  if (ptr == NULL || size == 0) {
    return CM_ERR_INVALID_ARGUMENT;
  }
  memset(ptr, 0, size);
  return CM_OK;
}

static cm_status sm_sort_init_merge_state(void* ptr, size_t size, void* ctx) {
  sm_sort_perm_init_ctx* init_ctx = (sm_sort_perm_init_ctx*)ctx;

  if (ptr == NULL || init_ctx == NULL || size != sizeof(sm_sort_merge_state)) {
    return CM_ERR_INVALID_ARGUMENT;
  }

  sm_sort_merge_state_reset((sm_sort_merge_state*)ptr, init_ctx->n_values);
  return CM_OK;
}

static cm_status sm_sort_init_heap_state(void* ptr, size_t size, void* ctx) {
  sm_sort_perm_init_ctx* init_ctx = (sm_sort_perm_init_ctx*)ctx;

  if (ptr == NULL || init_ctx == NULL || size != sizeof(sm_sort_heap_state)) {
    return CM_ERR_INVALID_ARGUMENT;
  }

  sm_sort_heap_state_reset((sm_sort_heap_state*)ptr, init_ctx->n_values);
  return CM_OK;
}

static size_t sm_sort_estimate_logical_size(sm_sort_algo algo, size_t n_values) {
  size_t needed = 0;
  long page_size_long;
  size_t page_size;

  page_size_long = sysconf(_SC_PAGESIZE);
  page_size = page_size_long > 0 ? (size_t)page_size_long : 4096u;

  needed += sm_sort_align_up(n_values * sizeof(int64_t), 8u);
  needed += sm_sort_align_up(n_values * sizeof(size_t), 8u);
  needed += sm_sort_align_up(sizeof(sm_sort_merge_state), 8u);
  needed += sm_sort_align_up(sizeof(sm_sort_heap_state), 8u);
  if (algo == SM_SORT_ALGO_MERGE) {
    needed += sm_sort_align_up(n_values * sizeof(size_t), 8u);
  }

  /* Reserve control slab page used by allocator metadata. */
  needed += page_size;
  needed = sm_sort_align_up(needed + page_size, page_size);
  if (needed < 2u * page_size) {
    needed = 2u * page_size;
  }
  return needed;
}

static sm_sort_status sm_sort_status_from_cm(cm_status status) {
  if (status == CM_OK) {
    return SM_SORT_OK;
  }
  if (status == CM_ERR_INVALID_ARGUMENT) {
    return SM_SORT_ERR_INVALID_ARGUMENT;
  }
  return SM_SORT_ERR_CM;
}

static sm_sort_status sm_sort_run_cm(const sm_sort_run_opts* opts, sm_sort_summary* out_summary) {
  sm_sort_status status;
  size_t n_values;
  cm_open_opts cm_opts;
  cm_status cm_status_code;
  int64_t* keys = NULL;
  size_t* perm = NULL;
  size_t* tmp = NULL;
  sm_sort_merge_state* merge_state = NULL;
  sm_sort_heap_state* heap_state = NULL;
  sm_sort_keys_init_ctx keys_ctx;
  sm_sort_perm_init_ctx size_ctx;
  sm_sort_commit_policy policy;
  int resumed = 0;

  memset(&policy, 0, sizeof(policy));

  status = sm_sort_count_values(opts->data_path, &n_values);
  if (status != SM_SORT_OK) {
    return status;
  }

  cm_opts.logical_size = sm_sort_estimate_logical_size(opts->algo, n_values);
  cm_opts.flags = 0;
  cm_status_code = cm_open(opts->shm_name, &cm_opts);
  if (cm_status_code != CM_OK) {
    return sm_sort_status_from_cm(cm_status_code);
  }

  keys_ctx.path = opts->data_path;
  keys_ctx.n_values = n_values;
  size_ctx.n_values = n_values;

  cm_status_code = cm_get_oralloc(
      "sort_keys", n_values * sizeof(*keys), sm_sort_init_keys_from_file, &keys_ctx, (void**)&keys);
  if (cm_status_code != CM_OK) {
    cm_close();
    return sm_sort_status_from_cm(cm_status_code);
  }

  cm_status_code = cm_get_oralloc(
      "sort_perm", n_values * sizeof(*perm), sm_sort_init_perm, &size_ctx, (void**)&perm);
  if (cm_status_code != CM_OK) {
    cm_close();
    return sm_sort_status_from_cm(cm_status_code);
  }

  policy.enabled = (opts->commit_every > 0) ? 1 : 0;
  policy.every = opts->commit_every;
  policy.commit_fn = sm_sort_commit_cm;

  if (opts->algo == SM_SORT_ALGO_MERGE) {
    cm_status_code = cm_get_oralloc(
        "sort_merge_tmp",
        n_values * sizeof(*tmp),
        sm_sort_init_merge_tmp,
        NULL,
        (void**)&tmp);
    if (cm_status_code != CM_OK) {
      cm_close();
      return sm_sort_status_from_cm(cm_status_code);
    }

    cm_status_code = cm_get_oralloc(
        "sort_merge_state",
        sizeof(*merge_state),
        sm_sort_init_merge_state,
        &size_ctx,
        (void**)&merge_state);
    if (cm_status_code != CM_OK) {
      cm_close();
      return sm_sort_status_from_cm(cm_status_code);
    }

    status = sm_sort_run_merge(keys, n_values, perm, tmp, merge_state, &policy, &resumed);
  } else {
    cm_status_code = cm_get_oralloc(
        "sort_heap_state",
        sizeof(*heap_state),
        sm_sort_init_heap_state,
        &size_ctx,
        (void**)&heap_state);
    if (cm_status_code != CM_OK) {
      cm_close();
      return sm_sort_status_from_cm(cm_status_code);
    }

    status = sm_sort_run_heap(keys, n_values, perm, heap_state, &policy, &resumed);
  }
  if (status != SM_SORT_OK) {
    cm_close();
    return status;
  }

  status = sm_sort_verify_permutation(keys, n_values, perm);
  if (status != SM_SORT_OK) {
    cm_close();
    return status;
  }

  if (out_summary != NULL) {
    out_summary->n_values = n_values;
    out_summary->n_commits = policy.commit_count;
    out_summary->resumed = resumed;
  }

  cm_close();
  return SM_SORT_OK;
}

sm_sort_status sm_sort_run(const sm_sort_run_opts* opts, sm_sort_summary* out_summary) {
  if (opts == NULL || opts->data_path == NULL) {
    return SM_SORT_ERR_INVALID_ARGUMENT;
  }
  if (opts->mode == SM_SORT_MODE_CM && opts->shm_name == NULL) {
    return SM_SORT_ERR_INVALID_ARGUMENT;
  }

  if (opts->mode == SM_SORT_MODE_CM) {
    return sm_sort_run_cm(opts, out_summary);
  }
  return sm_sort_run_normal(opts, out_summary);
}

const char* sm_sort_status_string(sm_sort_status status) {
  switch (status) {
    case SM_SORT_OK:
      return "SM_SORT_OK";
    case SM_SORT_ERR_INVALID_ARGUMENT:
      return "SM_SORT_ERR_INVALID_ARGUMENT";
    case SM_SORT_ERR_IO:
      return "SM_SORT_ERR_IO";
    case SM_SORT_ERR_ALLOC:
      return "SM_SORT_ERR_ALLOC";
    case SM_SORT_ERR_PARSE:
      return "SM_SORT_ERR_PARSE";
    case SM_SORT_ERR_VERIFY:
      return "SM_SORT_ERR_VERIFY";
    case SM_SORT_ERR_CM:
      return "SM_SORT_ERR_CM";
    default:
      return "SM_SORT_ERR_UNKNOWN";
  }
}
