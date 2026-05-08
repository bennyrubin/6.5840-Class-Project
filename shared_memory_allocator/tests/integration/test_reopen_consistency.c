#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

#include "cm/cm.h"

int main(void) {
  const char* shm_name = "/cm_integration_reopen_consistency";
  cm_open_opts opts;
  cm_status status;
  int fd;

  opts.logical_size = 1u << 20;
  opts.flags = 0;

  status = cm_open(shm_name, &opts);
  if (status != CM_OK) {
    fprintf(stderr, "expected first cm_open to succeed, got %s\n", cm_status_string(status));
    return 1;
  }

  cm_close();

  errno = 0;
  fd = shm_open(shm_name, O_RDWR, 0);
  if (fd != -1 || errno != ENOENT) {
    fprintf(stderr, "expected shm object to be unlinked after cm_close\n");
    if (fd != -1) {
      close(fd);
    }
    return 1;
  }

  status = cm_open(shm_name, &opts);
  if (status != CM_OK) {
    fprintf(stderr, "expected second cm_open to succeed, got %s\n", cm_status_string(status));
    return 1;
  }

  cm_close();
  return 0;
}
