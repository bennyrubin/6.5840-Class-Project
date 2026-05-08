#include <stdio.h>

#include "cm/cm.h"

int main(void) {
  cm_open_opts opts;
  cm_status status;

  opts.logical_size = 1u << 20;
  opts.flags = 0;

  status = cm_open(NULL, &opts);
  if (status != CM_ERR_INVALID_ARGUMENT) {
    fprintf(stderr, "expected CM_ERR_INVALID_ARGUMENT for NULL shm_name, got %s\n",
            cm_status_string(status));
    return 1;
  }

  status = cm_open("/cm_unit_open_invalid_args", NULL);
  if (status != CM_ERR_INVALID_ARGUMENT) {
    fprintf(stderr, "expected CM_ERR_INVALID_ARGUMENT for NULL opts, got %s\n",
            cm_status_string(status));
    return 1;
  }

  opts.logical_size = 0;
  status = cm_open("/cm_unit_open_invalid_args", &opts);
  if (status != CM_ERR_INVALID_ARGUMENT) {
    fprintf(stderr, "expected CM_ERR_INVALID_ARGUMENT for zero logical_size, got %s\n",
            cm_status_string(status));
    return 1;
  }

  opts.logical_size = 1u << 20;
  opts.flags = CM_OPEN_F_COMMIT_INTERVAL;
  opts.commit_interval = 0;
  status = cm_open("/cm_unit_open_invalid_args", &opts);
  if (status != CM_ERR_INVALID_ARGUMENT) {
    fprintf(stderr, "expected CM_ERR_INVALID_ARGUMENT for zero commit_interval, got %s\n",
            cm_status_string(status));
    return 1;
  }

  opts.flags = CM_OPEN_F_COMMIT_MEMORY_PRESSURE;
  opts.commit_memory_threshold_percent = 0;
  status = cm_open("/cm_unit_open_invalid_args", &opts);
  if (status != CM_ERR_INVALID_ARGUMENT) {
    fprintf(stderr, "expected CM_ERR_INVALID_ARGUMENT for zero memory threshold, got %s\n",
            cm_status_string(status));
    return 1;
  }

  opts.commit_memory_threshold_percent = 101;
  status = cm_open("/cm_unit_open_invalid_args", &opts);
  if (status != CM_ERR_INVALID_ARGUMENT) {
    fprintf(stderr, "expected CM_ERR_INVALID_ARGUMENT for high memory threshold, got %s\n",
            cm_status_string(status));
    return 1;
  }

  opts.flags = 1u << 31;
  status = cm_open("/cm_unit_open_invalid_args", &opts);
  if (status != CM_ERR_INVALID_ARGUMENT) {
    fprintf(stderr, "expected CM_ERR_INVALID_ARGUMENT for unknown flags, got %s\n",
            cm_status_string(status));
    return 1;
  }

  cm_close();
  return 0;
}
