#include "internal/undo_log.h"

#include <stdint.h>
#include <string.h>

#include "internal/debug.h"
#include "internal/mapping.h"
#include "internal/metadata.h"
#include "internal/trace.h"

static void* cm_undo_base_page(size_t page_index) {
  return (void*)((char*)cm_mapping_base() + (page_index * cm_mapping_page_size()));
}

static void* cm_undo_log_page(size_t page_index) {
  return (void*)((char*)cm_mapping_base() + cm_mapping_logical_size() +
                 (page_index * cm_mapping_page_size()));
}

cm_status cm_undo_log_append(size_t page_index) {
  cm_metadata_header* header;
  uint16_t* entries;
  size_t len;
  size_t capacity;
  size_t page_count;

  header = cm_metadata_header_ptr();
  entries = cm_metadata_undo_entries();
  page_count = cm_metadata_page_count();
  if (header == NULL || entries == NULL || page_count == 0) {
    return CM_ERR_IO;
  }

  capacity = header->undo_log_capacity;
  len = cm_metadata_undo_length_load(header);
  if (page_index >= page_count || page_index >= capacity || len > capacity) {
    return CM_ERR_CORRUPT_METADATA;
  }
  if (len == capacity) {
    return CM_ERR_IO;
  }

  entries[len] = (uint16_t)page_index;
  cm_metadata_undo_length_store(header, len + 1u);
  CM_DLOG("undo_log", "append", "append page index to undo log");
  return CM_OK;
}

cm_status cm_undo_log_replay(void) {
  cm_metadata_header* header;
  uint16_t* entries;
  size_t page_count;
  size_t len;

  header = cm_metadata_header_ptr();
  entries = cm_metadata_undo_entries();
  page_count = cm_metadata_page_count();
  if (header == NULL || entries == NULL || page_count == 0) {
    return CM_ERR_IO;
  }

  len = cm_metadata_undo_length_load(header);
  while (len > 0) {
    size_t page_index = entries[len - 1u];
    if (page_index >= page_count) {
      return CM_ERR_CORRUPT_METADATA;
    }
    memcpy(cm_undo_base_page(page_index), cm_undo_log_page(page_index), cm_mapping_page_size());
    cm_trace_maybe_abort("during_recovery_after_copy_before_pop");
    len -= 1u;
    cm_metadata_undo_length_store(header, len);
  }
  CM_DLOG("undo_log", "replay", "replay undo log entries");
  return CM_OK;
}

cm_status cm_undo_log_reset(void) {
  cm_metadata_header* header;

  header = cm_metadata_header_ptr();
  if (header == NULL) {
    return CM_ERR_IO;
  }
  cm_metadata_undo_length_store(header, 0);
  CM_DLOG("undo_log", "reset", "clear undo log for new epoch");
  return CM_OK;
}
