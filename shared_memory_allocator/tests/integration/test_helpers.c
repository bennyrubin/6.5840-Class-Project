#include "test_helpers.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../../src/internal/layout.h"

size_t th_align_up(size_t value, size_t alignment) {
  return cm_align_up(value, alignment);
}

long th_page_size(void) {
  return sysconf(_SC_PAGESIZE);
}

void th_cleanup_shm(const char* shm_name) {
  if (shm_name == NULL) {
    return;
  }
  (void)shm_unlink(shm_name);
}

int th_open_default(const char* shm_name, size_t logical_size) {
  cm_open_opts opts;
  cm_status status;

  opts.logical_size = logical_size;
  opts.flags = 0;
  status = cm_open(shm_name, &opts);
  return status == CM_OK ? 0 : -1;
}

int th_spawn_mode(
    const char* self_path,
    const char* mode,
    const char* shm_name,
    const char* crash_point,
    int* out_exit_code,
    int* out_term_sig) {
  pid_t pid;
  int status;

  if (self_path == NULL || mode == NULL || shm_name == NULL) {
    return -1;
  }

  pid = fork();
  if (pid < 0) {
    return -1;
  }
  if (pid == 0) {
    char* const argv_child[] = {
        (char*)self_path,
        (char*)mode,
        (char*)shm_name,
        NULL,
    };

    if (crash_point != NULL && crash_point[0] != '\0') {
      (void)setenv("CM_CRASH_POINT", crash_point, 1);
    } else {
      (void)unsetenv("CM_CRASH_POINT");
    }
    execv(self_path, argv_child);
    _exit(127);
  }

  if (waitpid(pid, &status, 0) < 0) {
    return -1;
  }
  if (out_exit_code != NULL) {
    *out_exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  }
  if (out_term_sig != NULL) {
    *out_term_sig = WIFSIGNALED(status) ? WTERMSIG(status) : 0;
  }
  return 0;
}

int th_shm_map_open(const char* shm_name, th_shm_map* out_map) {
  struct stat st;
  int fd;
  void* addr;

  if (shm_name == NULL || out_map == NULL) {
    return -1;
  }

  fd = shm_open(shm_name, O_RDWR, 0);
  if (fd < 0) {
    return -1;
  }
  if (fstat(fd, &st) != 0 || st.st_size <= 0) {
    close(fd);
    return -1;
  }

  addr = mmap(NULL, (size_t)st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (addr == MAP_FAILED) {
    close(fd);
    return -1;
  }

  out_map->fd = fd;
  out_map->addr = addr;
  out_map->size = (size_t)st.st_size;
  return 0;
}

void th_shm_map_close(th_shm_map* map) {
  if (map == NULL) {
    return;
  }
  if (map->addr != NULL && map->addr != MAP_FAILED && map->size > 0) {
    (void)munmap(map->addr, map->size);
  }
  if (map->fd >= 0) {
    (void)close(map->fd);
  }
  map->fd = -1;
  map->addr = NULL;
  map->size = 0;
}

cm_metadata_header* th_shm_metadata_header(th_shm_map* map, size_t logical_size) {
  long page_size;
  size_t aligned_logical_size;
  size_t metadata_offset;

  if (map == NULL || map->addr == NULL || map->size == 0) {
    return NULL;
  }

  page_size = th_page_size();
  if (page_size <= 0) {
    return NULL;
  }
  aligned_logical_size = th_align_up(logical_size, (size_t)page_size);
  metadata_offset = aligned_logical_size * 2u;
  if (metadata_offset + sizeof(cm_metadata_header) > map->size) {
    return NULL;
  }
  return (cm_metadata_header*)((char*)map->addr + metadata_offset);
}

unsigned char* th_base_ptr_from_header(const cm_metadata_header* header) {
  if (header == NULL || header->base_address == 0ULL) {
    return NULL;
  }
  return (unsigned char*)(uintptr_t)header->base_address;
}

unsigned char* th_log_ptr_from_header(const cm_metadata_header* header, size_t logical_size) {
  unsigned char* base_ptr;
  base_ptr = th_base_ptr_from_header(header);
  if (base_ptr == NULL) {
    return NULL;
  }
  return base_ptr + logical_size;
}

void th_fill_pattern(unsigned char* ptr, size_t len, unsigned char seed) {
  size_t i;
  if (ptr == NULL) {
    return;
  }
  for (i = 0; i < len; ++i) {
    ptr[i] = (unsigned char)(seed + (unsigned char)(i % 251u));
  }
}

int th_expect_pattern(const unsigned char* ptr, size_t len, unsigned char seed) {
  size_t i;
  if (ptr == NULL) {
    return -1;
  }
  for (i = 0; i < len; ++i) {
    if (ptr[i] != (unsigned char)(seed + (unsigned char)(i % 251u))) {
      return -1;
    }
  }
  return 0;
}
