#include <stdio.h>

#include "cm/cm.h"

int main(void) {
  cm_open_opts opts;
  cm_status status;
  void* ptr;

  opts.logical_size = 1u << 20;
  opts.flags = 0;

  status = cm_open("/cm_unit_alloc", &opts);
  if (status != CM_OK) {
    fprintf(stderr, "expected CM_OK from cm_open, got %s\n", cm_status_string(status));
    return 1;
  }

  ptr = cm_alloc(256);
  if (ptr == NULL) {
    fprintf(stderr, "expected non-NULL from cm_alloc\n");
    cm_close();
    return 1;
  }

  cm_close();
  return 0;
}
