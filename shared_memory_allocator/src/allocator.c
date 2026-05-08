#include "internal/allocator.h"

#include <stdint.h>
#include <string.h>

#include "internal/layout.h"
#include "internal/mapping.h"
#include "internal/metadata.h"
#include "internal/trace.h"

static void* cm_allocator_offset_to_ptr(size_t offset) {
  if (!cm_mapping_is_open() || offset >= cm_mapping_logical_size()) {
    return NULL;
  }
  return (void*)((char*)cm_mapping_base() + offset);
}

static int cm_allocator_ptr_to_offset(const void* ptr, size_t* out_offset) {
  size_t logical_size;
  const char* base;
  const char* p;

  if (!cm_mapping_is_open() || ptr == NULL || out_offset == NULL) {
    return 0;
  }

  base = (const char*)cm_mapping_base();
  p = (const char*)ptr;
  logical_size = cm_mapping_logical_size();
  if (base == NULL || p < base || (size_t)(p - base) >= logical_size) {
    return 0;
  }

  *out_offset = (size_t)(p - base);
  return 1;
}

static cm_alloc_block_header* cm_allocator_offset_to_header(size_t offset) {
  return (cm_alloc_block_header*)cm_allocator_offset_to_ptr(offset);
}

static void* cm_allocator_header_to_payload(cm_alloc_block_header* header) {
  if (header == NULL) {
    return NULL;
  }
  return (void*)((char*)header + sizeof(*header));
}

static int cm_allocator_block_valid(
    const cm_allocator_state* allocator,
    size_t block_offset,
    const cm_alloc_block_header* header) {
  if (allocator == NULL || header == NULL) {
    return 0;
  }
  if ((block_offset % 8u) != 0u || block_offset > allocator->arena_next_offset ||
      block_offset >= allocator->arena_limit) {
    return 0;
  }
  if (sizeof(*header) > allocator->arena_limit - block_offset) {
    return 0;
  }
  if (header->block_size < sizeof(*header) || (header->block_size % 8u) != 0u) {
    return 0;
  }
  if (header->block_size > allocator->arena_limit - block_offset) {
    return 0;
  }
  if (block_offset + header->block_size > allocator->arena_next_offset) {
    return 0;
  }
  return 1;
}

/* Resolves a named registry entry from a block header with consistency checks.
 * Returns 1 on success, 0 on any inconsistency (treat as corruption). */
static int cm_allocator_registry_entry_from_header(
    const cm_alloc_block_header* header,
    size_t payload_offset,
    cm_named_entry** out_entry,
    uint32_t* out_index) {
  cm_named_registry* registry;
  cm_named_entry* entries;
  uint32_t idx;

  if (header == NULL || out_entry == NULL || out_index == NULL) {
    return 0;
  }
  if (!header->is_named) {
    return 0;
  }

  registry = cm_metadata_named_registry();
  entries = cm_metadata_named_entries();
  if (registry == NULL || entries == NULL) {
    return 0;
  }

  idx = header->named_entry_index;
  if (idx >= registry->named_capacity) {
    return 0;
  }
  if (!entries[idx].in_use) {
    return 0;
  }
  if (entries[idx].offset != payload_offset) {
    return 0;
  }

  *out_entry = &entries[idx];
  *out_index = idx;
  return 1;
}

static int cm_allocator_payload_to_block(
    void* ptr,
    cm_allocator_state* allocator,
    size_t* out_block_offset,
    cm_alloc_block_header** out_header,
    size_t* out_payload_offset) {
  size_t payload_offset;
  size_t block_offset;
  cm_alloc_block_header* header;

  if (ptr == NULL || allocator == NULL || out_block_offset == NULL || out_header == NULL) {
    return 0;
  }
  if (!cm_allocator_ptr_to_offset(ptr, &payload_offset)) {
    return 0;
  }
  if (payload_offset < sizeof(cm_alloc_block_header)) {
    return 0;
  }

  block_offset = payload_offset - sizeof(cm_alloc_block_header);
  header = cm_allocator_offset_to_header(block_offset);
  if (header == NULL || !cm_allocator_block_valid(allocator, block_offset, header)) {
    return 0;
  }
  if (payload_offset != block_offset + sizeof(cm_alloc_block_header)) {
    return 0;
  }

  *out_block_offset = block_offset;
  *out_header = header;
  if (out_payload_offset != NULL) {
    *out_payload_offset = payload_offset;
  }
  return 1;
}

static void* cm_allocator_take_free_block(cm_allocator_state* allocator, size_t needed_size) {
  size_t prev_offset;
  size_t cur_offset;
  size_t guard;

  if (allocator == NULL || allocator->free_list_head_offset == CM_ALLOCATOR_OFFSET_NONE) {
    return NULL;
  }

  prev_offset = CM_ALLOCATOR_OFFSET_NONE;
  cur_offset = allocator->free_list_head_offset;
  guard = 0;

  while (cur_offset != CM_ALLOCATOR_OFFSET_NONE) {
    cm_alloc_block_header* cur_header;
    size_t next_offset;

    if (++guard > (allocator->arena_next_offset / 8u) + 1u) {
      return NULL;
    }

    cur_header = cm_allocator_offset_to_header(cur_offset);
    if (cur_header == NULL || !cm_allocator_block_valid(allocator, cur_offset, cur_header) ||
        !cur_header->is_free) {
      return NULL;
    }

    next_offset = cur_header->next_free_offset;
    if (cur_header->block_size >= needed_size) {
      if (prev_offset == CM_ALLOCATOR_OFFSET_NONE) {
        allocator->free_list_head_offset = next_offset;
      } else {
        cm_alloc_block_header* prev_header = cm_allocator_offset_to_header(prev_offset);
        if (prev_header == NULL || !cm_allocator_block_valid(allocator, prev_offset, prev_header) ||
            !prev_header->is_free) {
          return NULL;
        }
        prev_header->next_free_offset = next_offset;
      }

      cur_header->is_free = 0u;
      cur_header->next_free_offset = CM_ALLOCATOR_OFFSET_NONE;
      cur_header->is_named = 0u;
      cur_header->named_entry_index = CM_NAMED_ENTRY_INDEX_NONE;
      return cm_allocator_header_to_payload(cur_header);
    }

    prev_offset = cur_offset;
    cur_offset = next_offset;
  }

  return NULL;
}

void* cm_allocator_alloc(size_t size) {
  cm_allocator_state* allocator;
  size_t aligned_payload_size;
  size_t needed_size;
  size_t aligned_offset;
  size_t new_offset;
  cm_alloc_block_header* header;
  void* ptr;

  if (size == 0 || !cm_mapping_is_open()) {
    return NULL;
  }

  allocator = cm_metadata_allocator_state();
  if (allocator == NULL) {
    return NULL;
  }

  aligned_payload_size = cm_align_up(size, 8u);
  if (aligned_payload_size < size || aligned_payload_size > SIZE_MAX - sizeof(*header)) {
    return NULL;
  }
  needed_size = cm_align_up(sizeof(*header) + aligned_payload_size, 8u);
  if (needed_size < sizeof(*header) || needed_size < aligned_payload_size) {
    return NULL;
  }

  ptr = cm_allocator_take_free_block(allocator, needed_size);
  if (ptr != NULL) {
    return ptr;
  }

  aligned_offset = cm_align_up(allocator->arena_next_offset, 8u);
  if (aligned_offset < allocator->arena_next_offset) {
    return NULL;
  }
  if (needed_size > allocator->arena_limit - aligned_offset) {
    return NULL;
  }

  new_offset = aligned_offset + needed_size;
  header = cm_allocator_offset_to_header(aligned_offset);
  if (header == NULL) {
    return NULL;
  }

  header->block_size = needed_size;
  header->next_free_offset = CM_ALLOCATOR_OFFSET_NONE;
  header->is_free = 0u;
  header->is_named = 0u;
  header->named_entry_index = CM_NAMED_ENTRY_INDEX_NONE;
  header->reserved = 0u;

  allocator->arena_next_offset = new_offset;
  return cm_allocator_header_to_payload(header);
}

/* Internal free that pushes directly to the free list, skipping the named guard.
 * Used during named realloc when moving a named block to a new location. */
static void cm_allocator_free_block_internal(
    cm_allocator_state* allocator,
    size_t block_offset,
    cm_alloc_block_header* header) {
  if (allocator == NULL || header == NULL) {
    return;
  }
  if (header->is_free) {
    return;
  }

  header->is_free = 1u;
  header->next_free_offset = allocator->free_list_head_offset;
  header->is_named = 0u;
  header->named_entry_index = CM_NAMED_ENTRY_INDEX_NONE;
  allocator->free_list_head_offset = block_offset;
}

void cm_allocator_free(void* ptr) {
  cm_allocator_state* allocator;
  size_t block_offset;
  size_t payload_offset;
  cm_alloc_block_header* header;

  if (ptr == NULL || !cm_mapping_is_open()) {
    return;
  }

  allocator = cm_metadata_allocator_state();
  if (allocator == NULL) {
    return;
  }

  if (!cm_allocator_payload_to_block(ptr, allocator, &block_offset, &header, &payload_offset)) {
    return;
  }
  if (header->is_free || header->is_named) {
    return;
  }

  header->is_free = 1u;
  header->next_free_offset = allocator->free_list_head_offset;
  header->is_named = 0u;
  header->named_entry_index = CM_NAMED_ENTRY_INDEX_NONE;
  allocator->free_list_head_offset = block_offset;
}

void* cm_allocator_realloc(void* ptr, size_t size) {
  cm_allocator_state* allocator;
  size_t block_offset;
  size_t payload_offset;
  cm_alloc_block_header* header;
  size_t old_payload_capacity;
  void* new_ptr;

  if (ptr == NULL) {
    return cm_allocator_alloc(size);
  }
  if (!cm_mapping_is_open()) {
    return NULL;
  }

  allocator = cm_metadata_allocator_state();
  if (allocator == NULL) {
    return NULL;
  }
  if (!cm_allocator_payload_to_block(ptr, allocator, &block_offset, &header, &payload_offset) ||
      header->is_free) {
    return NULL;
  }

  /* Named realloc path */
  if (header->is_named) {
    cm_named_entry* entry;
    uint32_t idx;
    size_t new_payload_offset;
    cm_alloc_block_header* new_header;
    size_t new_block_offset;

    /* realloc(named_ptr, 0): per design, return NULL without deleting named entry */
    if (size == 0) {
      return NULL;
    }

    if (!cm_allocator_registry_entry_from_header(header, payload_offset, &entry, &idx)) {
      return NULL;
    }

    old_payload_capacity = header->block_size - sizeof(*header);

    /* Shrink or same size: update size in registry, return same ptr */
    if (size <= old_payload_capacity) {
      entry->size = size;
      return ptr;
    }

    /* Grow needed: allocate new block */
    new_ptr = cm_allocator_alloc(size);
    if (new_ptr == NULL) {
      return NULL;
    }

    memcpy(new_ptr, ptr, old_payload_capacity);

    /* Set named linkage on new block */
    if (!cm_allocator_ptr_to_offset(new_ptr, &new_payload_offset)) {
      /* Allocation succeeded but offset resolution failed — unexpected, but handle gracefully */
      cm_allocator_free(new_ptr);
      return NULL;
    }
    new_block_offset = new_payload_offset - sizeof(*header);
    new_header = cm_allocator_offset_to_header(new_block_offset);
    if (new_header == NULL) {
      cm_allocator_free(new_ptr);
      return NULL;
    }
    new_header->is_named = 1u;
    new_header->named_entry_index = idx;

    /* Update registry to point to new allocation */
    entry->offset = new_payload_offset;
    entry->size = size;

    /* Free old block internally (skip named guard since we already moved the entry) */
    cm_allocator_free_block_internal(allocator, block_offset, header);

    return new_ptr;
  }

  /* Unnamed realloc path */
  if (size == 0) {
    cm_allocator_free(ptr);
    return NULL;
  }

  old_payload_capacity = header->block_size - sizeof(*header);
  if (size <= old_payload_capacity) {
    return ptr;
  }

  new_ptr = cm_allocator_alloc(size);
  if (new_ptr == NULL) {
    return NULL;
  }

  memcpy(new_ptr, ptr, old_payload_capacity);
  cm_allocator_free(ptr);
  return new_ptr;
}

cm_status cm_allocator_get_oralloc(
    const char* name,
    size_t size,
    cm_init_fn init_fn,
    void* init_ctx,
    void** out_ptr) {
  cm_named_registry* registry;
  cm_named_entry* entries;
  cm_named_entry* free_entry = NULL;
  void* ptr = NULL;
  size_t i;
  cm_status status;

  if (name == NULL || name[0] == '\0' || out_ptr == NULL || size == 0 ||
      strlen(name) >= CM_NAMED_MAX_NAME) {
    return CM_ERR_INVALID_ARGUMENT;
  }
  if (!cm_mapping_is_open()) {
    return CM_ERR_IO;
  }

  registry = cm_metadata_named_registry();
  entries = cm_metadata_named_entries();
  if (registry == NULL || entries == NULL) {
    return CM_ERR_CORRUPT_METADATA;
  }

  for (i = 0; i < registry->named_capacity; ++i) {
    cm_named_entry* entry = &entries[i];
    if (!entry->in_use) {
      if (free_entry == NULL) {
        free_entry = entry;
      }
      continue;
    }
    if (strncmp(entry->name, name, CM_NAMED_MAX_NAME) != 0) {
      continue;
    }

    /* Name already exists — ignore size mismatch, proceed with existing entry */
    ptr = cm_allocator_offset_to_ptr(entry->offset);
    if (ptr == NULL) {
      return CM_ERR_CORRUPT_METADATA;
    }

    if (entry->state == CM_NAMED_READY) {
      *out_ptr = ptr;
      return CM_OK;
    }

    if (init_fn == NULL) {
      return CM_ERR_INVALID_ARGUMENT;
    }

    entry->state = CM_NAMED_INITIALIZING;
    status = init_fn(ptr, entry->size, init_ctx);
    if (status != CM_OK) {
      return status;
    }
    cm_trace_maybe_abort("during_named_initialization_before_ready");
    entry->state = CM_NAMED_READY;
    *out_ptr = ptr;
    return CM_OK;
  }

  if (free_entry == NULL) {
    return CM_ERR_IO;
  }

  ptr = cm_allocator_alloc(size);
  if (ptr == NULL) {
    return CM_ERR_IO;
  }

  /* Set named linkage in the block header */
  {
    size_t payload_offset;
    size_t block_offset;
    cm_alloc_block_header* block_header;
    uint32_t entry_index = (uint32_t)(free_entry - entries);

    if (cm_allocator_ptr_to_offset(ptr, &payload_offset) &&
        payload_offset >= sizeof(*block_header)) {
      block_offset = payload_offset - sizeof(*block_header);
      block_header = cm_allocator_offset_to_header(block_offset);
      if (block_header != NULL) {
        block_header->is_named = 1u;
        block_header->named_entry_index = entry_index;
      }
    }
  }

  memset(free_entry, 0, sizeof(*free_entry));
  free_entry->in_use = 1;
  free_entry->state = CM_NAMED_ALLOCATING;
  free_entry->offset = (size_t)((char*)ptr - (char*)cm_mapping_base());
  free_entry->size = size;
  memcpy(free_entry->name, name, strlen(name));
  registry->named_count += 1;

  free_entry->state = CM_NAMED_INITIALIZING;
  if (init_fn != NULL) {
    status = init_fn(ptr, size, init_ctx);
    if (status != CM_OK) {
      return status;
    }
  }
  cm_trace_maybe_abort("during_named_initialization_before_ready");
  free_entry->state = CM_NAMED_READY;
  *out_ptr = ptr;
  return CM_OK;
}
