#include <stdio.h>
#include <string.h>

#include "cm/cm.h"

static cm_status init_ok(void* ptr, size_t size, void* ctx) {
  (void)ctx;
  memset(ptr, 0, size);
  return CM_OK;
}

int main(void) {
  cm_open_opts opts;
  cm_status status;
  void* ptr = NULL;
  void* shrunk = NULL;
  void* ptr2 = NULL;

  opts.logical_size = 1u << 20;
  opts.flags = 0;

  status = cm_open("/cm_unit_named_realloc_shrink", &opts);
  if (status != CM_OK) {
    fprintf(stderr, "expected CM_OK from cm_open, got %s\n", cm_status_string(status));
    return 1;
  }

  status = cm_get_oralloc("shrink_buf", 256u, init_ok, NULL, &ptr);
  if (status != CM_OK || ptr == NULL) {
    fprintf(stderr, "expected CM_OK from initial cm_get_oralloc, got %s\n",
            cm_status_string(status));
    cm_close();
    return 1;
  }

  shrunk = cm_realloc(ptr, 64u);
  if (shrunk != ptr) {
    fprintf(stderr,
            "expected shrinking realloc on named ptr to return the same pointer\n");
    cm_close();
    return 1;
  }

  status = cm_get_oralloc("shrink_buf", 64u, NULL, NULL, &ptr2);
  if (status != CM_OK || ptr2 == NULL) {
    fprintf(stderr,
            "expected CM_OK from cm_get_oralloc after shrink (size 64), got %s\n",
            cm_status_string(status));
    cm_close();
    return 1;
  }

  cm_close();
  return 0;
}
