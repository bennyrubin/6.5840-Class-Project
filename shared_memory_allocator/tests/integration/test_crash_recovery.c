#include <stdio.h>

#include "cm/cm.h"

int main(void) {
  cm_open_opts opts;
  cm_status status;

  opts.logical_size = 4u << 20;
  opts.flags = 0;

  status = cm_open("/cm_integration_recovery", &opts);
  if (status != CM_OK) {
    fprintf(stderr, "integration expected cm_open success, got %s\n", cm_status_string(status));
    return 1;
  }

  status = cm_commit();
  if (status != CM_OK) {
    fprintf(stderr, "integration expected cm_commit success, got %s\n", cm_status_string(status));
    cm_close();
    return 1;
  }

  cm_close();
  return 0;
}
