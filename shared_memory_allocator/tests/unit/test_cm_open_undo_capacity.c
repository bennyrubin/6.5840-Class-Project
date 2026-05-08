#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cm/cm.h"
#include "../../src/internal/layout.h"
#include "../../src/internal/metadata.h"

int main(void) {
  cm_open_opts opts;
  cm_status status;
  long page_size;
  size_t aligned_logical_size;
  size_t expected_page_count;
  size_t expected_metadata_size;
  size_t expected_total_size;
  int fd = -1;
  struct stat st;

  opts.logical_size = 32u << 20;
  opts.flags = 0;

  page_size = sysconf(_SC_PAGESIZE);
  if (page_size <= 0) {
    fprintf(stderr, "sysconf(_SC_PAGESIZE) failed\n");
    return 1;
  }

  aligned_logical_size = cm_align_up(opts.logical_size, (size_t)page_size);
  expected_page_count = aligned_logical_size / (size_t)page_size;
  expected_metadata_size = cm_align_up(
      sizeof(cm_metadata_header) + (expected_page_count * sizeof(uint16_t)),
      (size_t)page_size);
  expected_total_size = (aligned_logical_size * 2u) + expected_metadata_size;

  status = cm_open("/cm_unit_open_undo_capacity", &opts);
  if (status != CM_OK) {
    fprintf(stderr, "expected CM_OK from larger cm_open, got %s\n", cm_status_string(status));
    cm_close();
    return 1;
  }

  fd = shm_open("/cm_unit_open_undo_capacity", O_RDWR, 0);
  if (fd == -1) {
    fprintf(stderr, "failed to open shm object for capacity inspection\n");
    cm_close();
    return 1;
  }
  if (fstat(fd, &st) != 0) {
    fprintf(stderr, "fstat failed\n");
    (void)close(fd);
    cm_close();
    return 1;
  }
  if ((size_t)st.st_size != expected_total_size) {
    fprintf(stderr, "unexpected total shm size: got %zu expected %zu\n", (size_t)st.st_size,
            expected_total_size);
    (void)close(fd);
    cm_close();
    return 1;
  }
  if (expected_metadata_size <= (size_t)page_size) {
    fprintf(stderr, "expected metadata size to grow beyond one page for this logical size\n");
    (void)close(fd);
    cm_close();
    return 1;
  }

  (void)close(fd);
  cm_close();
  return 0;
}
