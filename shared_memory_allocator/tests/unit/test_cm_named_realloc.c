#include <stdint.h>
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
  uint8_t* ptr = NULL;
  uint8_t* new_ptr = NULL;
  uint8_t* ptr2 = NULL;
  size_t i;

  opts.logical_size = 1u << 20;
  opts.flags = 0;

  status = cm_open("/cm_unit_named_realloc", &opts);
  if (status != CM_OK) {
    fprintf(stderr, "expected CM_OK from cm_open, got %s\n", cm_status_string(status));
    return 1;
  }

  status = cm_get_oralloc("buf", 64u, init_ok, NULL, (void**)&ptr);
  if (status != CM_OK || ptr == NULL) {
    fprintf(stderr, "expected CM_OK from initial cm_get_oralloc, got %s\n",
            cm_status_string(status));
    cm_close();
    return 1;
  }

  for (i = 0; i < 64u; ++i) {
    ptr[i] = 0xABu;
  }

  new_ptr = (uint8_t*)cm_realloc(ptr, 256u);
  if (new_ptr == NULL) {
    fprintf(stderr, "expected cm_realloc(named, grow) to return non-NULL\n");
    cm_close();
    return 1;
  }

  for (i = 0; i < 64u; ++i) {
    if (new_ptr[i] != 0xABu) {
      fprintf(stderr, "expected realloc to preserve first 64 bytes, mismatch at index %zu\n", i);
      cm_close();
      return 1;
    }
  }

  status = cm_get_oralloc("buf", 256u, NULL, NULL, (void**)&ptr2);
  if (status != CM_OK || ptr2 == NULL) {
    fprintf(stderr, "expected CM_OK from cm_get_oralloc after realloc grow, got %s\n",
            cm_status_string(status));
    cm_close();
    return 1;
  }
  if (ptr2 != new_ptr) {
    fprintf(stderr, "expected registry to return new_ptr after realloc grow\n");
    cm_close();
    return 1;
  }

  cm_close();
  return 0;
}
