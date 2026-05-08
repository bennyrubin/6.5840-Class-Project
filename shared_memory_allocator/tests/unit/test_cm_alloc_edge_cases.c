#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include "cm/cm.h"

int main(void) {
  cm_open_opts opts;
  cm_status status;
  long page_size;
  void* a;
  void* b;
  void* c;
  void* d;

  if (cm_alloc(0) != NULL) {
    fprintf(stderr, "expected cm_alloc(0) to return NULL\n");
    return 1;
  }
  if (cm_alloc(8) != NULL) {
    fprintf(stderr, "expected cm_alloc(size) without cm_open to return NULL\n");
    return 1;
  }

  page_size = sysconf(_SC_PAGESIZE);
  if (page_size <= 0) {
    fprintf(stderr, "sysconf(_SC_PAGESIZE) failed\n");
    return 1;
  }

  opts.logical_size = 1u << 20;
  opts.flags = 0;
  status = cm_open("/cm_unit_alloc_edge_alignment", &opts);
  if (status != CM_OK) {
    fprintf(stderr, "expected cm_open success for alignment case, got %s\n",
            cm_status_string(status));
    return 1;
  }

  a = cm_alloc(1);
  b = cm_alloc(1);
  c = cm_alloc(7);
  d = cm_alloc(8);
  if (a == NULL || b == NULL || c == NULL || d == NULL) {
    fprintf(stderr, "expected non-NULL allocations for alignment case\n");
    cm_close();
    return 1;
  }
  if (((uintptr_t)a % 8u) != 0u || ((uintptr_t)b % 8u) != 0u || ((uintptr_t)c % 8u) != 0u ||
      ((uintptr_t)d % 8u) != 0u) {
    fprintf(stderr, "expected all cm_alloc pointers to be 8-byte aligned\n");
    cm_close();
    return 1;
  }
  if (!((uintptr_t)a < (uintptr_t)b && (uintptr_t)b < (uintptr_t)c && (uintptr_t)c < (uintptr_t)d)) {
    fprintf(stderr, "expected allocations to be monotonic within arena\n");
    cm_close();
    return 1;
  }
  cm_close();

  opts.logical_size = (size_t)page_size * 2u;
  status = cm_open("/cm_unit_alloc_edge_exhaustion", &opts);
  if (status != CM_OK) {
    fprintf(stderr, "expected cm_open success for exhaustion case, got %s\n",
            cm_status_string(status));
    return 1;
  }

  a = cm_alloc((size_t)page_size / 2u);
  if (a == NULL) {
    fprintf(stderr, "expected first allocation in small arena to succeed\n");
    cm_close();
    return 1;
  }
  b = cm_alloc((size_t)page_size / 2u);
  if (b != NULL) {
    fprintf(stderr, "expected allocation after arena exhaustion to return NULL\n");
    cm_close();
    return 1;
  }

  cm_close();
  return 0;
}
