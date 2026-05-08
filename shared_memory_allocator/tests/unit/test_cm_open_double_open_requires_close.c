#include <stdio.h>

#include "cm/cm.h"

int main(void) {
  cm_open_opts opts;
  cm_status status;

  opts.logical_size = 1u << 20;
  opts.flags = 0;

  status = cm_open("/cm_unit_double_open_a", &opts);
  if (status != CM_OK) {
    fprintf(stderr, "expected first cm_open to return CM_OK, got %s\n", cm_status_string(status));
    return 1;
  }

  status = cm_open("/cm_unit_double_open_b", &opts);
  if (status != CM_ERR_INVALID_ARGUMENT) {
    fprintf(stderr,
            "expected second cm_open without close to return CM_ERR_INVALID_ARGUMENT, got %s\n",
            cm_status_string(status));
    cm_close();
    return 1;
  }

  cm_close();
  return 0;
}
