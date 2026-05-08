#ifndef CM_APP_SORT_H
#define CM_APP_SORT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum sm_sort_mode {
  SM_SORT_MODE_NORMAL = 0,
  SM_SORT_MODE_CM = 1,
} sm_sort_mode;

typedef enum sm_sort_algo {
  SM_SORT_ALGO_MERGE = 0,
  SM_SORT_ALGO_HEAP = 1,
} sm_sort_algo;

typedef enum sm_sort_status {
  SM_SORT_OK = 0,
  SM_SORT_ERR_INVALID_ARGUMENT = 1,
  SM_SORT_ERR_IO = 2,
  SM_SORT_ERR_ALLOC = 3,
  SM_SORT_ERR_PARSE = 4,
  SM_SORT_ERR_VERIFY = 5,
  SM_SORT_ERR_CM = 6,
} sm_sort_status;

typedef struct sm_sort_run_opts {
  sm_sort_mode mode;
  sm_sort_algo algo;
  const char* data_path;
  const char* shm_name;
  size_t commit_every;
} sm_sort_run_opts;

typedef struct sm_sort_summary {
  size_t n_values;
  size_t n_commits;
  int resumed;
} sm_sort_summary;

sm_sort_status sm_sort_run(const sm_sort_run_opts* opts, sm_sort_summary* out_summary);
const char* sm_sort_status_string(sm_sort_status status);

#ifdef __cplusplus
}
#endif

#endif
