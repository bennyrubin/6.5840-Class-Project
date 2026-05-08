#include "internal/metadata.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "internal/allocator.h"
#include "internal/layout.h"
#include "internal/mapping.h"

static int cm_control_layout_offsets(
    size_t control_size,
    size_t* out_registry_offset,
    size_t* out_allocator_offset,
    size_t* out_entries_offset,
    size_t* out_entries_capacity) {
  size_t offset;

  if (out_registry_offset == NULL || out_allocator_offset == NULL || out_entries_offset == NULL ||
      out_entries_capacity == NULL) {
    return 0;
  }
  if (control_size < sizeof(cm_named_registry) + sizeof(cm_allocator_state)) {
    return 0;
  }

  offset = 0;
  *out_registry_offset = offset;
  offset += sizeof(cm_named_registry);
  offset = cm_align_up(offset, 8u);
  if (offset > control_size) {
    return 0;
  }

  if (sizeof(cm_allocator_state) > control_size - offset) {
    return 0;
  }
  *out_allocator_offset = offset;
  offset += sizeof(cm_allocator_state);
  offset = cm_align_up(offset, 8u);
  if (offset > control_size) {
    return 0;
  }

  *out_entries_offset = offset;
  *out_entries_capacity = (control_size - offset) / sizeof(cm_named_entry);
  return 1;
}

static unsigned char* cm_control_base_ptr(void) {
  size_t logical_size;
  size_t control_size;
  unsigned char* base;

  if (!cm_mapping_is_open()) {
    return NULL;
  }
  logical_size = cm_mapping_logical_size();
  control_size = cm_metadata_control_slab_size();
  base = (unsigned char*)cm_mapping_base();
  if (base == NULL || control_size == 0 || control_size > logical_size) {
    return NULL;
  }
  return base + (logical_size - control_size);
}

static int cm_metadata_block_header_valid(
    const cm_allocator_state* allocator, size_t block_offset, const cm_alloc_block_header* header) {
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

static int cm_metadata_validate_free_list(const cm_allocator_state* allocator) {
  size_t cur_offset;
  size_t guard;

  if (allocator == NULL) {
    return 0;
  }
  if (allocator->free_list_head_offset == CM_ALLOCATOR_OFFSET_NONE) {
    return 1;
  }
  if (allocator->free_list_head_offset >= allocator->arena_next_offset) {
    return 0;
  }

  cur_offset = allocator->free_list_head_offset;
  guard = (allocator->arena_next_offset / 8u) + 1u;

  while (cur_offset != CM_ALLOCATOR_OFFSET_NONE) {
    cm_alloc_block_header* block =
        (cm_alloc_block_header*)((char*)cm_mapping_base() + cur_offset);

    if (guard == 0u) {
      return 0;
    }
    guard -= 1u;

    if (!cm_metadata_block_header_valid(allocator, cur_offset, block) || !block->is_free) {
      return 0;
    }
    if (block->next_free_offset != CM_ALLOCATOR_OFFSET_NONE &&
        block->next_free_offset >= allocator->arena_next_offset) {
      return 0;
    }

    cur_offset = block->next_free_offset;
  }

  return 1;
}

cm_metadata_header* cm_metadata_header_ptr(void) {
  if (!cm_mapping_is_open()) {
    return NULL;
  }
  return (cm_metadata_header*)cm_mapping_metadata_base();
}

size_t cm_metadata_page_count(void) {
  size_t page_size;
  size_t logical_size;

  page_size = cm_mapping_page_size();
  logical_size = cm_mapping_logical_size();
  if (page_size == 0) {
    return 0;
  }
  return logical_size / page_size;
}

size_t cm_metadata_undo_max_entries_fit(void) {
  size_t metadata_size;
  size_t header_size;

  metadata_size = cm_mapping_metadata_size();
  header_size = sizeof(cm_metadata_header);

  if (metadata_size < header_size) {
    return 0;
  }
  return (metadata_size - header_size) / sizeof(uint16_t);
}

size_t cm_metadata_control_slab_size(void) {
  if (!cm_mapping_is_open()) {
    return 0;
  }
  return cm_mapping_page_size();
}

size_t cm_metadata_control_offset(void) {
  size_t logical_size;
  size_t control_size;

  logical_size = cm_mapping_logical_size();
  control_size = cm_metadata_control_slab_size();
  if (control_size == 0 || control_size > logical_size) {
    return 0;
  }
  return logical_size - control_size;
}

uint16_t* cm_metadata_undo_entries(void) {
  cm_metadata_header* header;

  header = cm_metadata_header_ptr();
  if (header == NULL) {
    return NULL;
  }
  return (uint16_t*)((char*)header + sizeof(*header));
}

cm_named_registry* cm_metadata_named_registry(void) {
  unsigned char* control_base;
  size_t control_size;
  size_t registry_offset;
  size_t allocator_offset;
  size_t entries_offset;
  size_t entries_capacity;

  control_base = cm_control_base_ptr();
  control_size = cm_metadata_control_slab_size();
  if (control_base == NULL || control_size == 0) {
    return NULL;
  }
  if (!cm_control_layout_offsets(
          control_size,
          &registry_offset,
          &allocator_offset,
          &entries_offset,
          &entries_capacity)) {
    return NULL;
  }
  (void)allocator_offset;
  (void)entries_offset;
  (void)entries_capacity;
  return (cm_named_registry*)(control_base + registry_offset);
}

cm_allocator_state* cm_metadata_allocator_state(void) {
  unsigned char* control_base;
  size_t control_size;
  size_t registry_offset;
  size_t allocator_offset;
  size_t entries_offset;
  size_t entries_capacity;

  control_base = cm_control_base_ptr();
  control_size = cm_metadata_control_slab_size();
  if (control_base == NULL || control_size == 0) {
    return NULL;
  }
  if (!cm_control_layout_offsets(
          control_size,
          &registry_offset,
          &allocator_offset,
          &entries_offset,
          &entries_capacity)) {
    return NULL;
  }
  (void)registry_offset;
  (void)entries_offset;
  (void)entries_capacity;
  return (cm_allocator_state*)(control_base + allocator_offset);
}

cm_named_entry* cm_metadata_named_entries(void) {
  unsigned char* control_base;
  size_t control_size;
  size_t registry_offset;
  size_t allocator_offset;
  size_t entries_offset;
  size_t entries_capacity;

  control_base = cm_control_base_ptr();
  control_size = cm_metadata_control_slab_size();
  if (control_base == NULL || control_size == 0) {
    return NULL;
  }
  if (!cm_control_layout_offsets(
          control_size,
          &registry_offset,
          &allocator_offset,
          &entries_offset,
          &entries_capacity)) {
    return NULL;
  }
  (void)registry_offset;
  (void)allocator_offset;
  (void)entries_capacity;
  return (cm_named_entry*)(control_base + entries_offset);
}

cm_status cm_metadata_init(void) {
  cm_metadata_header* header;
  size_t page_count;
  size_t max_entries_fit;
  size_t control_size;
  unsigned char* control_base;
  size_t control_registry_offset;
  size_t control_allocator_offset;
  size_t control_entries_offset;
  size_t control_entries_capacity;
  size_t arena_limit;
  cm_named_registry* registry;
  cm_allocator_state* allocator;
  cm_named_entry* entries;

  if (!cm_mapping_is_open()) {
    return CM_ERR_IO;
  }
  if (!cm_mapping_is_fresh()) {
    return CM_OK;
  }

  header = (cm_metadata_header*)cm_mapping_metadata_base();
  if (header == NULL) {
    return CM_ERR_IO;
  }

  header->magic = CM_LAYOUT_MAGIC;
  header->version = CM_METADATA_VERSION;
  header->page_size = (unsigned int)cm_mapping_page_size();
  header->logical_size = cm_mapping_logical_size();
  header->base_address = (unsigned long long)(uintptr_t)cm_mapping_base();
  if (cm_mapping_page_size() == 0) {
    return CM_ERR_IO;
  }
  page_count = cm_metadata_page_count();
  max_entries_fit = cm_metadata_undo_max_entries_fit();
  if (page_count == 0 || page_count > max_entries_fit || page_count > UINT16_MAX) {
    return CM_ERR_INVALID_ARGUMENT;
  }
  control_size = cm_metadata_control_slab_size();
  control_base = cm_control_base_ptr();
  arena_limit = cm_metadata_control_offset();
  if (control_base == NULL || control_size == 0) {
    return CM_ERR_CORRUPT_METADATA;
  }
  if (!cm_control_layout_offsets(
          control_size,
          &control_registry_offset,
          &control_allocator_offset,
          &control_entries_offset,
          &control_entries_capacity)) {
    return CM_ERR_CORRUPT_METADATA;
  }

  header->undo_log_capacity = page_count;
  cm_metadata_undo_length_store(header, 0);

  memset(control_base, 0, control_size);
  registry = cm_metadata_named_registry();
  allocator = cm_metadata_allocator_state();
  entries = cm_metadata_named_entries();
  if (registry == NULL || allocator == NULL || entries == NULL) {
    return CM_ERR_CORRUPT_METADATA;
  }

  registry->named_count = 0;
  registry->named_capacity = control_entries_capacity;
  allocator->arena_next_offset = 0;
  allocator->arena_limit = arena_limit;
  allocator->free_list_head_offset = CM_ALLOCATOR_OFFSET_NONE;
  if (registry->named_capacity > 0) {
    memset(entries, 0, registry->named_capacity * sizeof(*entries));
  }
  return CM_OK;
}

cm_status cm_metadata_validate(void) {
  cm_metadata_header* header;
  size_t page_size;
  size_t logical_size;
  size_t page_count;
  size_t max_undo_entries_fit;
  size_t expected_control_size;
  size_t expected_arena_limit;
  size_t control_registry_offset;
  size_t control_allocator_offset;
  size_t control_entries_offset;
  size_t expected_named_capacity;
  size_t undo_len;
  size_t i;
  uint16_t* undo_entries;
  cm_named_registry* registry;
  cm_allocator_state* allocator;
  cm_named_entry* entries;

  if (!cm_mapping_is_open()) {
    return CM_ERR_IO;
  }

  header = (cm_metadata_header*)cm_mapping_metadata_base();
  page_size = cm_mapping_page_size();
  logical_size = cm_mapping_logical_size();
  if (header == NULL || page_size == 0 || logical_size == 0) {
    return CM_ERR_IO;
  }

  if (header->magic != CM_LAYOUT_MAGIC) {
    return CM_ERR_CORRUPT_METADATA;
  }
  if (header->version != CM_METADATA_VERSION) {
    return CM_ERR_CORRUPT_METADATA;
  }
  if ((size_t)header->page_size != page_size) {
    return CM_ERR_CORRUPT_METADATA;
  }
  if (header->logical_size != logical_size) {
    return CM_ERR_CORRUPT_METADATA;
  }
  if ((uintptr_t)header->base_address != (uintptr_t)cm_mapping_base()) {
    return CM_ERR_CORRUPT_METADATA;
  }
  if ((header->base_address % (unsigned long long)page_size) != 0ULL) {
    return CM_ERR_CORRUPT_METADATA;
  }

  page_count = logical_size / page_size;
  max_undo_entries_fit = cm_metadata_undo_max_entries_fit();
  if (header->undo_log_capacity == 0 || header->undo_log_capacity > page_count ||
      header->undo_log_capacity > max_undo_entries_fit || header->undo_log_capacity > UINT16_MAX) {
    return CM_ERR_CORRUPT_METADATA;
  }
  undo_len = cm_metadata_undo_length_load(header);
  if (undo_len > header->undo_log_capacity) {
    return CM_ERR_CORRUPT_METADATA;
  }

  undo_entries = cm_metadata_undo_entries();
  if (undo_entries == NULL) {
    return CM_ERR_CORRUPT_METADATA;
  }
  for (i = 0; i < undo_len; ++i) {
    if ((size_t)undo_entries[i] >= page_count) {
      return CM_ERR_CORRUPT_METADATA;
    }
  }

  registry = cm_metadata_named_registry();
  allocator = cm_metadata_allocator_state();
  entries = cm_metadata_named_entries();
  if (registry == NULL || allocator == NULL || entries == NULL) {
    return CM_ERR_CORRUPT_METADATA;
  }
  expected_control_size = cm_metadata_control_slab_size();
  expected_arena_limit = cm_metadata_control_offset();
  if (!cm_control_layout_offsets(
          expected_control_size,
          &control_registry_offset,
          &control_allocator_offset,
          &control_entries_offset,
          &expected_named_capacity)) {
    return CM_ERR_CORRUPT_METADATA;
  }
  (void)control_registry_offset;
  (void)control_allocator_offset;
  (void)control_entries_offset;
  if (allocator->arena_limit != expected_arena_limit) {
    return CM_ERR_CORRUPT_METADATA;
  }
  if ((allocator->arena_next_offset % 8u) != 0u) {
    return CM_ERR_CORRUPT_METADATA;
  }
  if (allocator->arena_next_offset > allocator->arena_limit ||
      allocator->arena_limit > logical_size) {
    return CM_ERR_CORRUPT_METADATA;
  }
  if (!cm_metadata_validate_free_list(allocator)) {
    return CM_ERR_CORRUPT_METADATA;
  }
  if (registry->named_capacity != expected_named_capacity) {
    return CM_ERR_CORRUPT_METADATA;
  }
  if (registry->named_count > registry->named_capacity) {
    return CM_ERR_CORRUPT_METADATA;
  }
  for (i = 0; i < registry->named_capacity; ++i) {
    if (!entries[i].in_use) {
      continue;
    }
    if (entries[i].offset >= allocator->arena_limit || entries[i].offset >= allocator->arena_next_offset ||
        entries[i].size > allocator->arena_limit - entries[i].offset) {
      return CM_ERR_CORRUPT_METADATA;
    }
    if (!(entries[i].state == CM_NAMED_ALLOCATING || entries[i].state == CM_NAMED_INITIALIZING ||
          entries[i].state == CM_NAMED_READY)) {
      return CM_ERR_CORRUPT_METADATA;
    }
    if (entries[i].name[0] == '\0') {
      return CM_ERR_CORRUPT_METADATA;
    }
    /* Validate block header linkage for named entries */
    if (entries[i].offset < sizeof(cm_alloc_block_header)) {
      return CM_ERR_CORRUPT_METADATA;
    }
    {
      size_t named_block_offset = entries[i].offset - sizeof(cm_alloc_block_header);
      const cm_alloc_block_header* named_header =
          (const cm_alloc_block_header*)((char*)cm_mapping_base() + named_block_offset);
      if (!cm_metadata_block_header_valid(allocator, named_block_offset, named_header)) {
        return CM_ERR_CORRUPT_METADATA;
      }
      if (named_header->is_free) {
        return CM_ERR_CORRUPT_METADATA;
      }
      if (!named_header->is_named) {
        return CM_ERR_CORRUPT_METADATA;
      }
      if (named_header->named_entry_index != (uint32_t)i) {
        return CM_ERR_CORRUPT_METADATA;
      }
      if (entries[i].size > named_header->block_size - sizeof(cm_alloc_block_header)) {
        return CM_ERR_CORRUPT_METADATA;
      }
    }
  }

  if (undo_len != header->undo_log_length) {
    return CM_ERR_CORRUPT_METADATA;
  }
  return CM_OK;
}
