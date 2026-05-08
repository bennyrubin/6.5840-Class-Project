#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sort.h"

#ifndef CM_SORT_DEFAULT_DATA_PATH
#define CM_SORT_DEFAULT_DATA_PATH "data/ids.csv"
#endif

static void sm_sort_print_usage(const char* argv0) {
  fprintf(
      stderr,
      "Usage: %s [--data PATH] [--algo merge|heap] [--mode normal|cm] [--shm-name NAME] "
      "[--commit-every N]\n",
      argv0);
}

int main(int argc, char** argv) {
  const char* data_path = CM_SORT_DEFAULT_DATA_PATH;
  const char* shm_name = "/cm_sort_app";
  sm_sort_algo algo = SM_SORT_ALGO_MERGE;
  sm_sort_mode mode = SM_SORT_MODE_NORMAL;
  size_t commit_every = 32;
  sm_sort_run_opts opts;
  sm_sort_summary summary;
  sm_sort_status status;
  size_t i;

  memset(&summary, 0, sizeof(summary));

  for (i = 1; i < (size_t)argc; ++i) {
    if (strcmp(argv[i], "--data") == 0 && i + 1 < (size_t)argc) {
      data_path = argv[++i];
    } else if (strcmp(argv[i], "--algo") == 0 && i + 1 < (size_t)argc) {
      const char* algo_arg = argv[++i];
      if (strcmp(algo_arg, "merge") == 0) {
        algo = SM_SORT_ALGO_MERGE;
      } else if (strcmp(algo_arg, "heap") == 0) {
        algo = SM_SORT_ALGO_HEAP;
      } else {
        sm_sort_print_usage(argv[0]);
        return 1;
      }
    } else if (strcmp(argv[i], "--mode") == 0 && i + 1 < (size_t)argc) {
      const char* mode_arg = argv[++i];
      if (strcmp(mode_arg, "normal") == 0) {
        mode = SM_SORT_MODE_NORMAL;
      } else if (strcmp(mode_arg, "cm") == 0) {
        mode = SM_SORT_MODE_CM;
      } else {
        sm_sort_print_usage(argv[0]);
        return 1;
      }
    } else if (strcmp(argv[i], "--shm-name") == 0 && i + 1 < (size_t)argc) {
      shm_name = argv[++i];
    } else if (strcmp(argv[i], "--commit-every") == 0 && i + 1 < (size_t)argc) {
      char* end = NULL;
      unsigned long parsed = strtoul(argv[++i], &end, 10);
      if (end == argv[i] || *end != '\0') {
        sm_sort_print_usage(argv[0]);
        return 1;
      }
      commit_every = (size_t)parsed;
    } else if (strcmp(argv[i], "--help") == 0) {
      sm_sort_print_usage(argv[0]);
      return 0;
    } else {
      sm_sort_print_usage(argv[0]);
      return 1;
    }
  }

  opts.mode = mode;
  opts.algo = algo;
  opts.data_path = data_path;
  opts.shm_name = shm_name;
  opts.commit_every = commit_every;

  status = sm_sort_run(&opts, &summary);
  if (status != SM_SORT_OK) {
    fprintf(stderr, "sort failed: %s\n", sm_sort_status_string(status));
    return 1;
  }

  printf("Sort (C)\n");
  printf("Data file      : %s\n", data_path);
  printf("Mode           : %s\n", mode == SM_SORT_MODE_CM ? "cm" : "normal");
  printf("Algorithm      : %s\n", algo == SM_SORT_ALGO_MERGE ? "merge" : "heap");
  printf("Values         : %zu\n", summary.n_values);
  printf("Resumed        : %s\n", summary.resumed ? "yes" : "no");
  if (mode == SM_SORT_MODE_CM) {
    if (commit_every == 0) {
      printf("Commit every   : disabled (0)\n");
    } else {
      printf("Commit every   : %zu\n", commit_every);
    }
    printf("Commit count   : %zu\n", summary.n_commits);
    printf("SHM name       : %s\n", shm_name);
  }
  printf("Verification   : passed\n");

  return 0;
}
