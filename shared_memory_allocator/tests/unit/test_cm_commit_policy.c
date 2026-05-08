#include <stdio.h>

#include "cm/cm.h"

int main(void) {
  cm_open_opts opts;
  cm_status status;

  opts.logical_size = 1u << 20;
  opts.flags = CM_OPEN_F_COMMIT_INTERVAL;
  opts.commit_interval = 3;
  opts.commit_memory_threshold_percent = 0;

  status = cm_open("/cm_unit_commit_policy", &opts);
  if (status != CM_OK) {
    fprintf(stderr, "expected CM_OK from cm_open, got %s\n", cm_status_string(status));
    return 1;
  }

  status = cm_commit();
  if (status != CM_COMMIT_SKIPPED_INTERVAL || !cm_commit_status_was_skipped(status) ||
      cm_status_is_error(status)) {
    fprintf(stderr, "expected first commit request to skip by interval, got %s\n",
            cm_status_string(status));
    cm_close();
    return 1;
  }

  status = cm_commit();
  if (status != CM_COMMIT_SKIPPED_INTERVAL) {
    fprintf(stderr, "expected second commit request to skip by interval, got %s\n",
            cm_status_string(status));
    cm_close();
    return 1;
  }

  status = cm_commit();
  if (status != CM_OK || !cm_commit_status_did_commit(status)) {
    fprintf(stderr, "expected third commit request to commit, got %s\n", cm_status_string(status));
    cm_close();
    return 1;
  }

  cm_close();
  return 0;
}
