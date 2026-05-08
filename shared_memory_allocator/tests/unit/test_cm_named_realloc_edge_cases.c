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
  void* ptr2 = NULL;
  void* result = NULL;

  /* --- Test 1: cm_realloc without cm_open returns NULL --- */
  result = cm_realloc(NULL, 64u);
  if (result != NULL) {
    fprintf(stderr, "expected cm_realloc(NULL, 64) without cm_open to return NULL\n");
    return 1;
  }

  opts.logical_size = 1u << 20;
  opts.flags = 0;

  status = cm_open("/cm_unit_named_realloc_edge_cases", &opts);
  if (status != CM_OK) {
    fprintf(stderr, "expected CM_OK from cm_open, got %s\n", cm_status_string(status));
    return 1;
  }

  /* --- Test 2: cm_realloc on named ptr with size=0 returns NULL,
                 entry is NOT deleted (get_oralloc still works) --- */
  status = cm_get_oralloc("zero_buf", 64u, init_ok, NULL, &ptr);
  if (status != CM_OK || ptr == NULL) {
    fprintf(stderr, "expected CM_OK for zero_buf initial alloc, got %s\n",
            cm_status_string(status));
    cm_close();
    return 1;
  }

  result = cm_realloc(ptr, 0u);
  if (result != NULL) {
    fprintf(stderr, "expected cm_realloc(named_ptr, 0) to return NULL\n");
    cm_close();
    return 1;
  }

  status = cm_get_oralloc("zero_buf", 64u, NULL, NULL, &ptr2);
  if (status != CM_OK || ptr2 == NULL) {
    fprintf(stderr,
            "expected named entry to still exist after cm_realloc(ptr, 0), got %s\n",
            cm_status_string(status));
    cm_close();
    return 1;
  }

  /* --- Test 3: cm_free on named ptr is a no-op;
                 ptr remains accessible and get_oralloc still works --- */
  status = cm_get_oralloc("free_noop_buf", 32u, init_ok, NULL, &ptr);
  if (status != CM_OK || ptr == NULL) {
    fprintf(stderr, "expected CM_OK for free_noop_buf initial alloc, got %s\n",
            cm_status_string(status));
    cm_close();
    return 1;
  }

  cm_free(ptr);

  status = cm_get_oralloc("free_noop_buf", 32u, NULL, NULL, &ptr2);
  if (status != CM_OK || ptr2 == NULL) {
    fprintf(stderr,
            "expected named entry to survive cm_free (no-op), got %s\n",
            cm_status_string(status));
    cm_close();
    return 1;
  }
  if (ptr2 != ptr) {
    fprintf(stderr,
            "expected get_oralloc after cm_free(named) to return original pointer\n");
    cm_close();
    return 1;
  }

  cm_close();
  return 0;
}
