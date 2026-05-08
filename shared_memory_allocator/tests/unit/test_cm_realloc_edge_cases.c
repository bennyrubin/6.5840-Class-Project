#include <stdio.h>

#include "cm/cm.h"

int main(void) {
  cm_open_opts opts;
  cm_status status;
  int local = 0;
  void* p;
  void* q;

  cm_free(NULL);
  if (cm_realloc(NULL, 0) != NULL) {
    fprintf(stderr, "expected cm_realloc(NULL, 0) to return NULL\n");
    return 1;
  }
  if (cm_realloc(NULL, 8) != NULL) {
    fprintf(stderr, "expected cm_realloc without cm_open to fail\n");
    return 1;
  }

  opts.logical_size = 1u << 20;
  opts.flags = 0;
  status = cm_open("/cm_unit_realloc_edge_cases", &opts);
  if (status != CM_OK) {
    fprintf(stderr, "expected cm_open success, got %s\n", cm_status_string(status));
    return 1;
  }

  p = cm_realloc(NULL, 24);
  if (p == NULL) {
    fprintf(stderr, "expected cm_realloc(NULL, size) to allocate\n");
    cm_close();
    return 1;
  }

  q = cm_realloc(p, 0);
  if (q != NULL) {
    fprintf(stderr, "expected cm_realloc(ptr, 0) to return NULL\n");
    cm_close();
    return 1;
  }

  p = cm_alloc(16);
  if (p == NULL) {
    fprintf(stderr, "expected allocation after realloc(ptr,0) to succeed\n");
    cm_close();
    return 1;
  }

  q = cm_realloc(&local, 16);
  if (q != NULL) {
    fprintf(stderr, "expected realloc on non-allocator pointer to fail\n");
    cm_close();
    return 1;
  }
  cm_free(&local);

  cm_close();
  return 0;
}
