#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>

#include "cm/cm.h"

int main(void) {
  cm_open_opts opts;
  cm_status status;
  int fd;

  opts.logical_size = 1u << 20;
  opts.flags = 0;

  status = cm_open("/cm_unit_close_unlinks", &opts);
  if (status != CM_OK) {
    fprintf(stderr, "expected CM_OK from cm_open, got %s\n", cm_status_string(status));
    return 1;
  }

  cm_close();

  fd = shm_open("/cm_unit_close_unlinks", O_RDWR, 0);
  if (fd != -1) {
    fprintf(stderr, "expected shm object to be unlinked by cm_close\n");
    return 1;
  }

  return 0;
}
