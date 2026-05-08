#include <stdio.h>

#include "cm/cm.h"

int main(void) {
  cm_open_opts opts;
  cm_status status;
  void* a;
  void* b;
  void* c;

  opts.logical_size = 1u << 20;
  opts.flags = 0;

  status = cm_open("/cm_unit_free", &opts);
  if (status != CM_OK) {
    fprintf(stderr, "expected CM_OK from cm_open, got %s\n", cm_status_string(status));
    return 1;
  }

  a = cm_alloc(64);
  b = cm_alloc(64);
  if (a == NULL || b == NULL) {
    fprintf(stderr, "expected initial allocations to succeed\n");
    cm_close();
    return 1;
  }

  cm_free(a);
  c = cm_alloc(32);
  if (c == NULL) {
    fprintf(stderr, "expected allocation after free to succeed\n");
    cm_close();
    return 1;
  }
  if (c != a) {
    fprintf(stderr, "expected freed block to be reused first\n");
    cm_close();
    return 1;
  }

  cm_free(NULL);
  cm_close();
  return 0;
}
