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
  void* ptr1 = NULL;
  void* new_ptr = NULL;
  void* unnamed = NULL;

  opts.logical_size = 1u << 20;
  opts.flags = 0;

  status = cm_open("/cm_unit_named_realloc_free_list_reuse", &opts);
  if (status != CM_OK) {
    fprintf(stderr, "expected CM_OK from cm_open, got %s\n", cm_status_string(status));
    return 1;
  }

  status = cm_get_oralloc("grow_me", 64u, init_ok, NULL, &ptr1);
  if (status != CM_OK || ptr1 == NULL) {
    fprintf(stderr, "expected CM_OK from cm_get_oralloc(grow_me), got %s\n",
            cm_status_string(status));
    cm_close();
    return 1;
  }

  new_ptr = cm_realloc(ptr1, 256u);
  if (new_ptr == NULL) {
    fprintf(stderr, "expected cm_realloc(named, grow) to succeed\n");
    cm_close();
    return 1;
  }

  /* The old 64-byte block should have been released to the free list.
     A subsequent unnamed alloc of that size should succeed and must not
     collide with the new named allocation. */
  unnamed = cm_alloc(64u);
  if (unnamed == NULL) {
    fprintf(stderr, "expected cm_alloc(64) to succeed after named realloc grow\n");
    cm_close();
    return 1;
  }
  if (unnamed == new_ptr) {
    fprintf(stderr,
            "expected unnamed alloc to differ from new named pointer after realloc\n");
    cm_close();
    return 1;
  }

  cm_close();
  return 0;
}
