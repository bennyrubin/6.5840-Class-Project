#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cm/cm.h"
#include "../../src/internal/layout.h"
#include "../../src/internal/metadata.h"

int main(void) {
  const char* shm_name = "/cm_unit_metadata_corruption";
  cm_open_opts opts;
  cm_status status;
  long page_size;
  size_t aligned_logical_size;
  size_t metadata_offset;
  size_t total_size;
  int fd = -1;
  void* mapped = MAP_FAILED;
  cm_metadata_header* header;

  opts.logical_size = 1u << 20;
  opts.flags = 0;

  page_size = sysconf(_SC_PAGESIZE);
  if (page_size <= 0) {
    fprintf(stderr, "sysconf(_SC_PAGESIZE) failed\n");
    return 1;
  }
  aligned_logical_size = cm_align_up(opts.logical_size, (size_t)page_size);
  metadata_offset = aligned_logical_size * 2u;
  total_size = metadata_offset + (size_t)page_size;

  (void)shm_unlink(shm_name);
  fd = shm_open(shm_name, O_CREAT | O_EXCL | O_RDWR, 0600);
  if (fd == -1) {
    fprintf(stderr, "failed to create shm object for corruption test\n");
    return 1;
  }
  if (ftruncate(fd, (off_t)total_size) != 0) {
    fprintf(stderr, "ftruncate failed\n");
    close(fd);
    (void)shm_unlink(shm_name);
    return 1;
  }

  mapped = mmap(NULL, total_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (mapped == MAP_FAILED) {
    fprintf(stderr, "mmap failed\n");
    close(fd);
    (void)shm_unlink(shm_name);
    return 1;
  }
  memset(mapped, 0, total_size);
  header = (cm_metadata_header*)((char*)mapped + metadata_offset);
  header->magic = 0;
  header->version = CM_METADATA_VERSION;
  header->page_size = (unsigned int)page_size;
  header->logical_size = aligned_logical_size;
  header->base_address = 0ULL;
  header->undo_log_capacity = aligned_logical_size / (size_t)page_size;
  header->undo_log_length = 0;

  munmap(mapped, total_size);
  close(fd);

  status = cm_open(shm_name, &opts);
  if (status != CM_ERR_CORRUPT_METADATA) {
    fprintf(stderr, "expected CM_ERR_CORRUPT_METADATA from cm_open, got %s\n",
            cm_status_string(status));
    cm_close();
    (void)shm_unlink(shm_name);
    return 1;
  }

  cm_close();
  (void)shm_unlink(shm_name);
  return 0;
}
