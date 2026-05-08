#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cm/cm.h"
#include "../../src/internal/layout.h"
#include "../../src/internal/metadata.h"

static int read_base_address(const char* shm_name, size_t logical_size, unsigned long long* out_base) {
  long page_size;
  size_t aligned_logical_size;
  size_t metadata_offset;
  int fd = -1;
  struct stat st;
  void* mapped = MAP_FAILED;
  cm_metadata_header* header;

  if (shm_name == NULL || out_base == NULL) {
    return 1;
  }

  page_size = sysconf(_SC_PAGESIZE);
  if (page_size <= 0) {
    fprintf(stderr, "sysconf(_SC_PAGESIZE) failed\n");
    return 1;
  }

  aligned_logical_size = cm_align_up(logical_size, (size_t)page_size);
  metadata_offset = aligned_logical_size * 2u;

  fd = shm_open(shm_name, O_RDWR, 0);
  if (fd == -1) {
    fprintf(stderr, "failed to open shm object %s for metadata inspection\n", shm_name);
    return 1;
  }

  if (fstat(fd, &st) != 0) {
    fprintf(stderr, "fstat failed for %s\n", shm_name);
    close(fd);
    return 1;
  }

  mapped = mmap(NULL, (size_t)st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (mapped == MAP_FAILED) {
    fprintf(stderr, "mmap failed for %s\n", shm_name);
    close(fd);
    return 1;
  }

  header = (cm_metadata_header*)((char*)mapped + metadata_offset);
  *out_base = header->base_address;

  munmap(mapped, (size_t)st.st_size);
  close(fd);
  return 0;
}

int main(void) {
  cm_open_opts opts;
  cm_status status;
  unsigned long long first_base = 0;
  unsigned long long second_base = 0;
  long page_size;

  opts.logical_size = 1u << 20;
  opts.flags = 0;

  status = cm_open("/cm_unit_open_deterministic_base_a", &opts);
  if (status != CM_OK) {
    fprintf(stderr, "expected first cm_open to succeed, got %s\n", cm_status_string(status));
    return 1;
  }
  if (read_base_address("/cm_unit_open_deterministic_base_a", opts.logical_size, &first_base) != 0) {
    cm_close();
    return 1;
  }
  cm_close();

  status = cm_open("/cm_unit_open_deterministic_base_b", &opts);
  if (status != CM_OK) {
    fprintf(stderr, "expected second cm_open to succeed, got %s\n", cm_status_string(status));
    return 1;
  }
  if (read_base_address("/cm_unit_open_deterministic_base_b", opts.logical_size, &second_base) != 0) {
    cm_close();
    return 1;
  }
  cm_close();

  if (first_base == 0ULL || second_base == 0ULL) {
    fprintf(stderr, "expected non-zero base addresses in metadata\n");
    return 1;
  }
  if (first_base != second_base) {
    fprintf(stderr, "expected deterministic base address, got %llu and %llu\n", first_base,
            second_base);
    return 1;
  }

  page_size = sysconf(_SC_PAGESIZE);
  if (page_size <= 0) {
    fprintf(stderr, "sysconf(_SC_PAGESIZE) failed\n");
    return 1;
  }
  if ((first_base % (unsigned long long)page_size) != 0ULL) {
    fprintf(stderr, "base address is not page aligned: %llu\n", first_base);
    return 1;
  }

  return 0;
}
