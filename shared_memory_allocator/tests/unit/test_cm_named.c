#include <stdio.h>

#include "cm/cm.h"

static cm_status seed_init(void* ptr, size_t size, void* ctx) {
  (void)ptr;
  (void)size;
  (void)ctx;
  return CM_OK;
}

int main(void) {
  cm_open_opts opts;
  cm_status status;
  void* ptr = NULL;

  opts.logical_size = 1u << 20;
  opts.flags = 0;

  status = cm_open("/cm_unit_named", &opts);
  if (status != CM_OK) {
    fprintf(stderr, "expected CM_OK from cm_open, got %s\n", cm_status_string(status));
    return 1;
  }

  status = cm_get_oralloc("root", 4096, seed_init, NULL, &ptr);
  if (status != CM_OK) {
    fprintf(stderr, "expected CM_OK from cm_get_oralloc, got %s\n", cm_status_string(status));
    cm_close();
    return 1;
  }

  cm_close();
  return 0;
}
