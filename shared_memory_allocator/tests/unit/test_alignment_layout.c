#include <fcntl.h>
#include <stdint.h>
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
  size_t log_offset;
  size_t metadata_offset;
  int fd = -1;
  struct stat st;
  void* mapped = MAP_FAILED;
  cm_metadata_header* header;
  uintptr_t header_addr;

  opts.logical_size = (2u << 20) + 11u;
  opts.flags = 0;

  status = cm_open("/cm_unit_alignment_layout", &opts);
  if (status != CM_OK) {
    fprintf(stderr, "expected CM_OK from cm_open, got %s\n", cm_status_string(status));
    return 1;
  }

  page_size = sysconf(_SC_PAGESIZE);
  if (page_size <= 0) {
    fprintf(stderr, "sysconf(_SC_PAGESIZE) failed\n");
    cm_close();
    return 1;
  }

  aligned_logical_size = cm_align_up(opts.logical_size, (size_t)page_size);
  log_offset = aligned_logical_size;
  metadata_offset = aligned_logical_size * 2u;

  if (log_offset % (size_t)page_size != 0) {
    fprintf(stderr, "log offset is not page aligned: %zu\n", log_offset);
    cm_close();
    return 1;
  }
  if (metadata_offset % (size_t)page_size != 0) {
    fprintf(stderr, "metadata offset is not page aligned: %zu\n", metadata_offset);
    cm_close();
    return 1;
  }

  fd = shm_open("/cm_unit_alignment_layout", O_RDWR, 0);
  if (fd == -1) {
    fprintf(stderr, "failed to open shm for alignment inspection\n");
    cm_close();
    return 1;
  }
  if (fstat(fd, &st) != 0) {
    fprintf(stderr, "fstat failed\n");
    close(fd);
    cm_close();
    return 1;
  }

  mapped = mmap(NULL, (size_t)st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (mapped == MAP_FAILED) {
    fprintf(stderr, "mmap failed\n");
    close(fd);
    cm_close();
    return 1;
  }

  header = (cm_metadata_header*)((char*)mapped + metadata_offset);
  header_addr = (uintptr_t)header;
  if (header_addr % 8u != 0u) {
    fprintf(stderr, "metadata header is not 8-byte aligned: %lu\n", (unsigned long)header_addr);
    munmap(mapped, (size_t)st.st_size);
    close(fd);
    cm_close();
    return 1;
  }
  if (metadata_offset + sizeof(*header) > (size_t)st.st_size) {
    fprintf(stderr, "metadata header does not fit in shm object\n");
    munmap(mapped, (size_t)st.st_size);
    close(fd);
    cm_close();
    return 1;
  }

  munmap(mapped, (size_t)st.st_size);
  close(fd);
  cm_close();
  return 0;
}
