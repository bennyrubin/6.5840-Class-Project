#ifndef CM_INTERNAL_METADATA_H
#define CM_INTERNAL_METADATA_H

#include <stdint.h>
#include <stdatomic.h>

#include "cm/cm.h"

typedef struct cm_metadata_header {
  unsigned long long magic;
  unsigned int version;
  unsigned int page_size;
  size_t logical_size;
  unsigned long long base_address;
  size_t undo_log_capacity;
  size_t undo_log_length;
} cm_metadata_header;

#define CM_METADATA_VERSION 5u
#define CM_NAMED_MAX_NAME 64u

typedef struct cm_allocator_state {
  size_t arena_next_offset;
  size_t arena_limit;
  size_t free_list_head_offset;
} cm_allocator_state;

typedef struct cm_named_entry {
  uint32_t in_use;
  uint32_t state;
  size_t offset;
  size_t size;
  char name[CM_NAMED_MAX_NAME];
} cm_named_entry;

typedef struct cm_named_registry {
  size_t named_count;
  size_t named_capacity;
} cm_named_registry;

cm_status cm_metadata_init(void);
cm_status cm_metadata_validate(void);

cm_metadata_header* cm_metadata_header_ptr(void);
size_t cm_metadata_page_count(void);
size_t cm_metadata_undo_max_entries_fit(void);
size_t cm_metadata_control_slab_size(void);
size_t cm_metadata_control_offset(void);
uint16_t* cm_metadata_undo_entries(void);
cm_allocator_state* cm_metadata_allocator_state(void);
cm_named_registry* cm_metadata_named_registry(void);
cm_named_entry* cm_metadata_named_entries(void);

static inline size_t cm_metadata_undo_length_load(const cm_metadata_header* header) {
  const _Atomic size_t* atomic_len;
  if (header == NULL) {
    return 0;
  }
  atomic_len = (const _Atomic size_t*)&header->undo_log_length;
  return atomic_load_explicit(atomic_len, memory_order_acquire);
}

static inline void cm_metadata_undo_length_store(cm_metadata_header* header, size_t value) {
  _Atomic size_t* atomic_len;
  if (header == NULL) {
    return;
  }
  atomic_len = (_Atomic size_t*)&header->undo_log_length;
  atomic_store_explicit(atomic_len, value, memory_order_release);
}

#endif
