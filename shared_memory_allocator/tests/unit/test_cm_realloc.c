#include <stdint.h>
#include <stdio.h>

#include "cm/cm.h"

int main(void) {
  cm_open_opts opts;
  cm_status status;
  uint8_t* p;
  uint8_t* q;
  uint8_t* r;
  size_t i;

  opts.logical_size = 1u << 20;
  opts.flags = 0;

  status = cm_open("/cm_unit_realloc", &opts);
  if (status != CM_OK) {
    fprintf(stderr, "expected CM_OK from cm_open, got %s\n", cm_status_string(status));
    return 1;
  }

  p = (uint8_t*)cm_alloc(16);
  if (p == NULL) {
    fprintf(stderr, "expected initial cm_alloc to succeed\n");
    cm_close();
    return 1;
  }
  for (i = 0; i < 16u; ++i) {
    p[i] = (uint8_t)(0x40u + (uint8_t)i);
  }

  q = (uint8_t*)cm_realloc(p, 128);
  if (q == NULL) {
    fprintf(stderr, "expected growing realloc to succeed\n");
    cm_close();
    return 1;
  }
  for (i = 0; i < 16u; ++i) {
    if (q[i] != (uint8_t)(0x40u + (uint8_t)i)) {
      fprintf(stderr, "expected realloc to preserve old data when growing\n");
      cm_close();
      return 1;
    }
  }

  r = (uint8_t*)cm_realloc(q, 8);
  if (r != q) {
    fprintf(stderr, "expected shrinking realloc to keep the same pointer\n");
    cm_close();
    return 1;
  }
  for (i = 0; i < 8u; ++i) {
    if (r[i] != (uint8_t)(0x40u + (uint8_t)i)) {
      fprintf(stderr, "expected shrinking realloc to preserve leading bytes\n");
      cm_close();
      return 1;
    }
  }

  cm_close();
  return 0;
}
