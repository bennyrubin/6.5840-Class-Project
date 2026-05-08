#include <stdio.h>

#include "cm/cm.h"

int main(void) {
  cm_open_opts opts;
  cm_status status;

  opts.logical_size = 8u << 20;
  opts.flags = 0;

  status = cm_open("/cm_example_minimal", &opts);
  if (status != CM_OK) {
    fprintf(stderr, "cm_open failed: %s\n", cm_status_string(status));
    return 1;
  }

  if (cm_alloc(128) == NULL) {
    fprintf(stderr, "cm_alloc failed\n");
    cm_close();
    return 1;
  }

  status = cm_commit();
  if (status != CM_OK) {
    fprintf(stderr, "cm_commit failed: %s\n", cm_status_string(status));
    cm_close();
    return 1;
  }

  cm_close();
  return 0;
}
