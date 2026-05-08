#include <stdio.h>

#include "cm/cm.h"

int main(void) {
  cm_open_opts opts;
  cm_status status;
  unsigned char* buf;
  unsigned char* tmp;
  size_t i;

  opts.logical_size = 2u << 20;
  opts.flags = 0;

  status = cm_open("/cm_writer_app", &opts);
  if (status != CM_OK) {
    fprintf(stderr, "simple_writer_app: cm_open failed: %s\n", cm_status_string(status));
    return 1;
  }

  buf = (unsigned char*)cm_alloc(64u);
  if (buf == NULL) {
    fprintf(stderr, "simple_writer_app: cm_alloc failed\n");
    cm_close();
    return 1;
  }
  for (i = 0; i < 64u; ++i) {
    buf[i] = (unsigned char)(i + 1u);
  }

  buf = (unsigned char*)cm_realloc(buf, 128u);
  if (buf == NULL) {
    fprintf(stderr, "simple_writer_app: cm_realloc failed\n");
    cm_close();
    return 1;
  }
  for (i = 64u; i < 128u; ++i) {
    buf[i] = (unsigned char)(0x80u + (unsigned char)(i - 64u));
  }

  tmp = (unsigned char*)cm_alloc(32u);
  if (tmp == NULL) {
    fprintf(stderr, "simple_writer_app: second cm_alloc failed\n");
    cm_close();
    return 1;
  }
  cm_free(tmp);

  status = cm_commit();
  if (status != CM_OK) {
    fprintf(stderr, "simple_writer_app: cm_commit failed: %s\n", cm_status_string(status));
    cm_close();
    return 1;
  }

  cm_close();
  return 0;
}
