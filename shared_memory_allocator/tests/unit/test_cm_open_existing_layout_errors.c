#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cm/cm.h"

static int create_shm_with_size(const char* shm_name, size_t size) {
  int fd;

  (void)shm_unlink(shm_name);
  fd = shm_open(shm_name, O_CREAT | O_EXCL | O_RDWR, 0600);
  if (fd == -1) {
    return -1;
  }
  if (ftruncate(fd, (off_t)size) != 0) {
    (void)close(fd);
    (void)shm_unlink(shm_name);
    return -1;
  }
  if (close(fd) != 0) {
    (void)shm_unlink(shm_name);
    return -1;
  }
  return 0;
}

int main(void) {
  cm_open_opts opts;
  cm_status status;
  long page_size;
  size_t existing_logical_size;
  size_t existing_total_size;

  page_size = sysconf(_SC_PAGESIZE);
  if (page_size <= 0) {
    fprintf(stderr, "sysconf(_SC_PAGESIZE) failed\n");
    return 1;
  }

  opts.logical_size = 1u << 20;
  opts.flags = 0;

  existing_logical_size = 2u << 20;
  existing_total_size = (existing_logical_size * 2u) + (size_t)page_size;
  if (create_shm_with_size("/cm_unit_open_existing_mismatch", existing_total_size) != 0) {
    fprintf(stderr, "failed to create pre-existing shm object for mismatch test\n");
    return 1;
  }

  status = cm_open("/cm_unit_open_existing_mismatch", &opts);
  if (status != CM_ERR_INVALID_ARGUMENT) {
    fprintf(stderr, "expected size mismatch to return invalid-argument, got %s\n",
            cm_status_string(status));
    cm_close();
    (void)shm_unlink("/cm_unit_open_existing_mismatch");
    return 1;
  }
  cm_close();
  (void)shm_unlink("/cm_unit_open_existing_mismatch");

  if (create_shm_with_size("/cm_unit_open_existing_odd_payload", (size_t)page_size + 1u) != 0) {
    fprintf(stderr, "failed to create pre-existing shm object for odd-payload test\n");
    return 1;
  }

  status = cm_open("/cm_unit_open_existing_odd_payload", &opts);
  if (status != CM_ERR_CORRUPT_METADATA) {
    fprintf(stderr, "expected odd payload layout to return corrupt-metadata, got %s\n",
            cm_status_string(status));
    cm_close();
    (void)shm_unlink("/cm_unit_open_existing_odd_payload");
    return 1;
  }
  cm_close();
  (void)shm_unlink("/cm_unit_open_existing_odd_payload");

  return 0;
}
