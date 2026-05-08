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
  size_t expected_total_size;
  size_t metadata_offset;
  int fd = -1;
  struct stat st;
  void* mapped = MAP_FAILED;
  cm_metadata_header* header;

  opts.logical_size = (1u << 20) + 123u;
  opts.flags = 0;

  status = cm_open("/cm_unit_metadata_layout", &opts);
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
  metadata_offset = aligned_logical_size * 2u;
  expected_total_size = metadata_offset + (size_t)page_size;

  fd = shm_open("/cm_unit_metadata_layout", O_RDWR, 0);
  if (fd == -1) {
    fprintf(stderr, "failed to open shm for inspection\n");
    cm_close();
    return 1;
  }

  if (fstat(fd, &st) != 0) {
    fprintf(stderr, "fstat failed\n");
    close(fd);
    cm_close();
    return 1;
  }

  if ((size_t)st.st_size != expected_total_size) {
    fprintf(stderr, "unexpected shm size: got %zu expected %zu\n", (size_t)st.st_size,
            expected_total_size);
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
  if (header->magic != CM_LAYOUT_MAGIC) {
    fprintf(stderr, "unexpected metadata magic: got %llu\n", header->magic);
    munmap(mapped, (size_t)st.st_size);
    close(fd);
    cm_close();
    return 1;
  }
  if (header->version != CM_METADATA_VERSION) {
    fprintf(stderr, "unexpected metadata version: got %u expected %u\n", header->version,
            CM_METADATA_VERSION);
    munmap(mapped, (size_t)st.st_size);
    close(fd);
    cm_close();
    return 1;
  }
  if (header->page_size != (unsigned int)page_size) {
    fprintf(stderr, "unexpected metadata page_size: got %u expected %ld\n", header->page_size,
            page_size);
    munmap(mapped, (size_t)st.st_size);
    close(fd);
    cm_close();
    return 1;
  }
  if (header->logical_size != aligned_logical_size) {
    fprintf(stderr, "unexpected metadata logical_size: got %zu expected %zu\n", header->logical_size,
            aligned_logical_size);
    munmap(mapped, (size_t)st.st_size);
    close(fd);
    cm_close();
    return 1;
  }
  if (header->undo_log_capacity != aligned_logical_size / (size_t)page_size) {
    fprintf(stderr, "unexpected undo_log_capacity: got %zu expected %zu\n",
            header->undo_log_capacity, aligned_logical_size / (size_t)page_size);
    munmap(mapped, (size_t)st.st_size);
    close(fd);
    cm_close();
    return 1;
  }
  if (header->base_address == 0ULL) {
    fprintf(stderr, "expected non-zero metadata base_address\n");
    munmap(mapped, (size_t)st.st_size);
    close(fd);
    cm_close();
    return 1;
  }
  if ((header->base_address % (unsigned long long)page_size) != 0ULL) {
    fprintf(stderr, "metadata base_address is not page-aligned: %llu\n", header->base_address);
    munmap(mapped, (size_t)st.st_size);
    close(fd);
    cm_close();
    return 1;
  }
  if (header->undo_log_length != 0) {
    fprintf(stderr, "expected undo_log_length to start at 0, got %zu\n", header->undo_log_length);
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
