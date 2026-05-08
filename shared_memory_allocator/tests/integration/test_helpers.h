#ifndef CM_TEST_HELPERS_H
#define CM_TEST_HELPERS_H

#include <stddef.h>

#include "cm/cm.h"
#include "../../src/internal/metadata.h"

typedef struct th_shm_map {
  int fd;
  void* addr;
  size_t size;
} th_shm_map;

size_t th_align_up(size_t value, size_t alignment);
long th_page_size(void);
void th_cleanup_shm(const char* shm_name);
int th_open_default(const char* shm_name, size_t logical_size);
int th_spawn_mode(
    const char* self_path,
    const char* mode,
    const char* shm_name,
    const char* crash_point,
    int* out_exit_code,
    int* out_term_sig);

int th_shm_map_open(const char* shm_name, th_shm_map* out_map);
void th_shm_map_close(th_shm_map* map);
cm_metadata_header* th_shm_metadata_header(th_shm_map* map, size_t logical_size);
unsigned char* th_base_ptr_from_header(const cm_metadata_header* header);
unsigned char* th_log_ptr_from_header(const cm_metadata_header* header, size_t logical_size);

void th_fill_pattern(unsigned char* ptr, size_t len, unsigned char seed);
int th_expect_pattern(const unsigned char* ptr, size_t len, unsigned char seed);

#endif
