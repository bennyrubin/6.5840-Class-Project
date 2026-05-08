#include <stdio.h>

#include "cm/cm.h"

int main(void) {
  cm_open_opts opts;
  cm_status status;
  void* ptr;

  opts.logical_size = 2u << 20;
  opts.flags = 0;

  status = cm_open("/cm_writer_app", &opts);
  if (status != CM_OK) {
    fprintf(stderr, "simple_verify_app: cm_open failed: %s\n", cm_status_string(status));
    return 1;
  }

  ptr = cm_alloc(48u);
  if (ptr == NULL) {
    fprintf(stderr, "simple_verify_app: cm_alloc failed\n");
    cm_close();
    return 1;
  }
  ptr = cm_realloc(ptr, 96u);
  if (ptr == NULL) {
    fprintf(stderr, "simple_verify_app: cm_realloc failed\n");
    cm_close();
    return 1;
  }
  cm_free(ptr);

  cm_close();
  return 0;
}
