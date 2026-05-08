#ifndef CM_INTERNAL_ALLOCATOR_H
#define CM_INTERNAL_ALLOCATOR_H

#include <stddef.h>
#include <stdint.h>

#include "cm/cm.h"

#define CM_ALLOCATOR_OFFSET_NONE ((size_t)SIZE_MAX)

typedef struct cm_alloc_block_header {
  size_t block_size;
  size_t next_free_offset;
  uint32_t is_free;
  uint32_t is_named;
  uint32_t named_entry_index;
  uint32_t reserved;
} cm_alloc_block_header;

#define CM_NAMED_ENTRY_INDEX_NONE UINT32_MAX

void* cm_allocator_alloc(size_t size);
void cm_allocator_free(void* ptr);
void* cm_allocator_realloc(void* ptr, size_t size);
cm_status cm_allocator_get_oralloc(
    const char* name,
    size_t size,
    cm_init_fn init_fn,
    void* init_ctx,
    void** out_ptr);

#endif
