#include "internal/mapping.h"

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "internal/layout.h"
#include "internal/metadata.h"
#include "internal/debug.h"

typedef struct cm_mapping_state {
  int is_open;
  int is_fresh;
  int shm_fd;
  void* mapped_base;
  size_t mapped_size;
  size_t page_size;
  size_t logical_size;
  size_t metadata_offset;
  size_t metadata_size;
} cm_mapping_state;

static cm_mapping_state g_mapping_state;

#if defined(__linux__) && !defined(MAP_FIXED_NOREPLACE)
#define MAP_FIXED_NOREPLACE 0x100000
#endif

static const uintptr_t g_cm_base_candidates[] = {
    0x500000000000ULL,
    0x540000000000ULL,
    0x580000000000ULL,
    0x5c0000000000ULL,
};

static cm_status cm_mapping_compute_metadata_size(
    size_t logical_size, size_t page_size, size_t* out_metadata_size) {
  size_t page_count;
  size_t required;
  size_t metadata_size;

  if (logical_size == 0 || page_size == 0 || out_metadata_size == NULL) {
    return CM_ERR_INVALID_ARGUMENT;
  }
  if ((logical_size % page_size) != 0u) {
    return CM_ERR_INVALID_ARGUMENT;
  }

  page_count = logical_size / page_size;
  if (page_count == 0 || page_count > UINT16_MAX) {
    return CM_ERR_INVALID_ARGUMENT;
  }
  if (page_count > ((SIZE_MAX - sizeof(cm_metadata_header)) / sizeof(uint16_t))) {
    return CM_ERR_INVALID_ARGUMENT;
  }

  required = sizeof(cm_metadata_header) + (page_count * sizeof(uint16_t));
  metadata_size = cm_align_up(required, page_size);
  if (metadata_size < required || metadata_size < page_size) {
    return CM_ERR_INVALID_ARGUMENT;
  }

  *out_metadata_size = metadata_size;
  return CM_OK;
}

static void cm_mapping_reset_state(void) {
  g_mapping_state.is_open = 0;
  g_mapping_state.is_fresh = 0;
  g_mapping_state.shm_fd = -1;
  g_mapping_state.mapped_base = NULL;
  g_mapping_state.mapped_size = 0;
  g_mapping_state.page_size = 0;
  g_mapping_state.logical_size = 0;
  g_mapping_state.metadata_offset = 0;
  g_mapping_state.metadata_size = 0;
}

static cm_status cm_mapping_compute_sizes(
    const cm_open_opts* opts,
    size_t page_size,
    size_t* out_aligned_logical_size,
    size_t* out_metadata_size,
    size_t* out_total_size) {
  size_t aligned_logical_size;
  size_t metadata_size;
  size_t total_size;
  cm_status status;

  if (opts == NULL || out_aligned_logical_size == NULL || out_metadata_size == NULL ||
      out_total_size == NULL) {
    return CM_ERR_INVALID_ARGUMENT;
  }
  if (opts->logical_size == 0 || page_size == 0) {
    return CM_ERR_INVALID_ARGUMENT;
  }

  aligned_logical_size = cm_align_up(opts->logical_size, page_size);
  if (aligned_logical_size < opts->logical_size) {
    return CM_ERR_INVALID_ARGUMENT;
  }

  status = cm_mapping_compute_metadata_size(aligned_logical_size, page_size, &metadata_size);
  if (status != CM_OK) {
    return CM_ERR_INVALID_ARGUMENT;
  }

  if (aligned_logical_size > ((SIZE_MAX - metadata_size) / 2u)) {
    return CM_ERR_INVALID_ARGUMENT;
  }
  total_size = (aligned_logical_size * 2u) + metadata_size;
  if (total_size < metadata_size) {
    return CM_ERR_INVALID_ARGUMENT;
  }

  *out_aligned_logical_size = aligned_logical_size;
  *out_metadata_size = metadata_size;
  *out_total_size = total_size;
  return CM_OK;
}

static cm_status cm_mapping_read_header(
    int shm_fd, size_t mapped_size, size_t metadata_offset, cm_metadata_header* out_header) {
  off_t metadata_file_offset;
  ssize_t read_bytes;

  if (out_header == NULL || metadata_offset > mapped_size ||
      (mapped_size - metadata_offset) < sizeof(*out_header)) {
    return CM_ERR_CORRUPT_METADATA;
  }

  metadata_file_offset = (off_t)metadata_offset;
  read_bytes = pread(shm_fd, out_header, sizeof(*out_header), metadata_file_offset);
  if (read_bytes < 0) {
    return CM_ERR_IO;
  }
  if ((size_t)read_bytes != sizeof(*out_header)) {
    return CM_ERR_CORRUPT_METADATA;
  }
  return CM_OK;
}

static void* cm_mapping_try_map_with_base(int shm_fd, size_t mapped_size, uintptr_t base_address) {
#ifdef MAP_FIXED_NOREPLACE
  void* mapped = mmap(
      (void*)base_address,
      mapped_size,
      PROT_READ | PROT_WRITE,
      MAP_SHARED | MAP_FIXED_NOREPLACE,
      shm_fd,
      0);
  if (mapped != MAP_FAILED || errno != EINVAL) {
    return mapped;
  }
#else
  (void)shm_fd;
  (void)mapped_size;
  (void)base_address;
#endif

  /* Fallback for environments where MAP_FIXED_NOREPLACE is unavailable:
     map at the requested address with MAP_FIXED. */
  return mmap(
      (void*)base_address, mapped_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, shm_fd, 0);
}

static void* cm_mapping_map_fresh(int shm_fd, size_t mapped_size) {
  size_t i;

  for (i = 0; i < (sizeof(g_cm_base_candidates) / sizeof(g_cm_base_candidates[0])); ++i) {
    void* mapped = cm_mapping_try_map_with_base(shm_fd, mapped_size, g_cm_base_candidates[i]);
    if (mapped != MAP_FAILED) {
      return mapped;
    }
  }

  /* If fixed candidates are unavailable, fall back to kernel-selected base. */
  return mmap(NULL, mapped_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
}

static cm_status cm_mapping_get_existing_base(
    int shm_fd,
    size_t mapped_size,
    size_t metadata_offset,
    size_t page_size,
    size_t logical_size,
    uintptr_t* out_base) {
  cm_metadata_header header;
  cm_status status;

  if (out_base == NULL) {
    return CM_ERR_INVALID_ARGUMENT;
  }

  status = cm_mapping_read_header(shm_fd, mapped_size, metadata_offset, &header);
  if (status != CM_OK) {
    return status;
  }

  if (header.magic != CM_LAYOUT_MAGIC) {
    return CM_ERR_CORRUPT_METADATA;
  }
  if (header.version != CM_METADATA_VERSION) {
    return CM_ERR_CORRUPT_METADATA;
  }
  if ((size_t)header.page_size != page_size) {
    return CM_ERR_CORRUPT_METADATA;
  }
  if (header.logical_size != logical_size) {
    return CM_ERR_CORRUPT_METADATA;
  }
  if (header.base_address == 0ULL) {
    return CM_ERR_CORRUPT_METADATA;
  }
  if ((header.base_address % (unsigned long long)page_size) != 0ULL) {
    return CM_ERR_CORRUPT_METADATA;
  }

  *out_base = (uintptr_t)header.base_address;
  return CM_OK;
}

cm_status cm_mapping_open(const char* shm_name, const cm_open_opts* opts) {
  long sys_page_size;
  cm_status status;
  size_t requested_logical_size;
  size_t requested_metadata_size;
  size_t requested_total_size;
  size_t logical_size;
  size_t metadata_size;
  size_t metadata_offset;
  size_t mapped_size;
  int is_fresh = 0;
  int shm_fd = -1;
  struct stat st;
  void* mapped_base = MAP_FAILED;
  uintptr_t mapped_base_address = 0;

  if (shm_name == NULL || opts == NULL) {
    CM_DLOG("mapping", "open_fail_invalid_args", "shm_name or opts is null");
    return CM_ERR_INVALID_ARGUMENT;
  }
  if (g_mapping_state.is_open) {
    CM_DLOG("mapping", "open_fail_already_open", "mapping already open");
    return CM_ERR_INVALID_ARGUMENT;
  }

  sys_page_size = sysconf(_SC_PAGESIZE);
  if (sys_page_size <= 0) {
    CM_DLOG("mapping", "open_fail_sys_page_size", "sysconf(_SC_PAGESIZE) failed");
    return CM_ERR_IO;
  }

  status = cm_mapping_compute_sizes(
      opts,
      (size_t)sys_page_size,
      &requested_logical_size,
      &requested_metadata_size,
      &requested_total_size);
  if (status != CM_OK) {
    CM_DLOG("mapping", "open_fail_compute_sizes", "cm_mapping_compute_sizes failed");
    return status;
  }

  shm_fd = shm_open(shm_name, O_RDWR | O_CREAT | O_EXCL, 0600);
  if (shm_fd >= 0) {
    is_fresh = 1;
  } else if (errno == EEXIST) {
    shm_fd = shm_open(shm_name, O_RDWR, 0600);
  }
  if (shm_fd < 0) {
    CM_DLOG("mapping", "open_fail_shm_open", "shm_open failed");
    return CM_ERR_IO;
  }

  if (is_fresh) {
    if (ftruncate(shm_fd, (off_t)requested_total_size) != 0) {
      CM_DLOG("mapping", "open_fail_ftruncate", "ftruncate failed");
      (void)close(shm_fd);
      (void)shm_unlink(shm_name);
      return CM_ERR_IO;
    }
    mapped_size = requested_total_size;
    logical_size = requested_logical_size;
    metadata_size = requested_metadata_size;
  } else {
    if (fstat(shm_fd, &st) != 0) {
      CM_DLOG("mapping", "open_fail_fstat", "fstat failed");
      (void)close(shm_fd);
      return CM_ERR_IO;
    }
    if (st.st_size <= 0) {
      CM_DLOG("mapping", "open_fail_bad_size_nonpositive", "existing shm size <= 0");
      (void)close(shm_fd);
      return CM_ERR_CORRUPT_METADATA;
    }

    mapped_size = (size_t)st.st_size;
    if (mapped_size <= (size_t)sys_page_size) {
      CM_DLOG("mapping", "open_fail_bad_size_too_small", "existing shm size too small");
      (void)close(shm_fd);
      return CM_ERR_CORRUPT_METADATA;
    }

    if (mapped_size < requested_total_size) {
      CM_DLOG("mapping", "open_fail_existing_too_small", "existing shm smaller than requested");
      (void)close(shm_fd);
      return CM_ERR_CORRUPT_METADATA;
    }
    if (mapped_size > requested_total_size) {
      CM_DLOG("mapping", "open_fail_existing_too_large", "existing shm larger than requested");
      (void)close(shm_fd);
      return CM_ERR_INVALID_ARGUMENT;
    }
    logical_size = requested_logical_size;
    metadata_size = requested_metadata_size;
    metadata_offset = logical_size * 2u;

    status = cm_mapping_get_existing_base(
        shm_fd,
        mapped_size,
        metadata_offset,
        (size_t)sys_page_size,
        logical_size,
        &mapped_base_address);
    if (status != CM_OK) {
      CM_DLOG("mapping", "open_fail_existing_base", "failed to load existing base from metadata");
      (void)close(shm_fd);
      return status;
    }
  }

  metadata_offset = logical_size * 2u;

  if (is_fresh) {
    mapped_base = cm_mapping_map_fresh(shm_fd, mapped_size);
  } else {
    mapped_base = cm_mapping_try_map_with_base(shm_fd, mapped_size, mapped_base_address);
  }
  if (mapped_base == MAP_FAILED) {
    CM_DLOG("mapping", "open_fail_mmap", "mmap failed");
    if (is_fresh) {
      (void)close(shm_fd);
      (void)shm_unlink(shm_name);
    } else {
      (void)close(shm_fd);
    }
    return CM_ERR_IO;
  }

  cm_mapping_reset_state();
  g_mapping_state.is_open = 1;
  g_mapping_state.is_fresh = is_fresh;
  g_mapping_state.shm_fd = shm_fd;
  g_mapping_state.mapped_base = mapped_base;
  g_mapping_state.mapped_size = mapped_size;
  g_mapping_state.page_size = (size_t)sys_page_size;
  g_mapping_state.logical_size = logical_size;
  g_mapping_state.metadata_offset = metadata_offset;
  g_mapping_state.metadata_size = metadata_size;
  return CM_OK;
}

cm_status cm_mapping_close(void) {
  cm_status status = CM_OK;

  if (!g_mapping_state.is_open) {
    return CM_OK;
  }

  if (g_mapping_state.mapped_base != NULL && g_mapping_state.mapped_size > 0) {
    if (munmap(g_mapping_state.mapped_base, g_mapping_state.mapped_size) != 0) {
      status = CM_ERR_IO;
    }
  }
  if (g_mapping_state.shm_fd >= 0) {
    if (close(g_mapping_state.shm_fd) != 0) {
      status = CM_ERR_IO;
    }
  }

  cm_mapping_reset_state();
  return status;
}

int cm_mapping_is_open(void) {
  return g_mapping_state.is_open;
}

int cm_mapping_is_fresh(void) {
  return g_mapping_state.is_fresh;
}

size_t cm_mapping_page_size(void) {
  return g_mapping_state.page_size;
}

size_t cm_mapping_logical_size(void) {
  return g_mapping_state.logical_size;
}

size_t cm_mapping_total_size(void) {
  return g_mapping_state.mapped_size;
}

size_t cm_mapping_metadata_size(void) {
  return g_mapping_state.metadata_size;
}

void* cm_mapping_base(void) {
  return g_mapping_state.mapped_base;
}

void* cm_mapping_metadata_base(void) {
  if (!g_mapping_state.is_open || g_mapping_state.mapped_base == NULL) {
    return NULL;
  }
  return (void*)((char*)g_mapping_state.mapped_base + g_mapping_state.metadata_offset);
}
