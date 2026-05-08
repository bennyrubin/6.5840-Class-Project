#include <stdio.h>

#include "cm/cm.h"

static cm_status init_counter(void* ptr, size_t size, void* ctx) {
  (void)ctx;
  if (ptr == NULL || size < sizeof(int)) {
    return CM_ERR_INVALID_ARGUMENT;
  }
  *((int*)ptr) = 1;
  return CM_OK;
}

int main(void) {
  cm_open_opts opts;
  cm_status status;
  void* ptr = NULL;

  opts.logical_size = 8u << 20;
  opts.flags = 0;

  status = cm_open("/cm_example_named", &opts);
  if (status != CM_OK) {
    fprintf(stderr, "cm_open failed: %s\n", cm_status_string(status));
    return 1;
  }

  status = cm_get_oralloc("counter", sizeof(int), init_counter, NULL, &ptr);
  if (status != CM_OK) {
    fprintf(stderr, "cm_get_oralloc failed: %s\n", cm_status_string(status));
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
