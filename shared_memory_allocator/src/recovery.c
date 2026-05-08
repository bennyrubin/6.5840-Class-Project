#include "internal/recovery.h"

#include "internal/debug.h"
#include "internal/metadata.h"
#include "internal/undo_log.h"

cm_status cm_recover_if_needed(void) {
  cm_metadata_header* header;
  size_t undo_len;

  header = cm_metadata_header_ptr();
  if (header == NULL) {
    return CM_ERR_IO;
  }
  undo_len = cm_metadata_undo_length_load(header);
  if (undo_len == 0) {
    return CM_OK;
  }
  CM_DLOG("recovery", "begin", "replay undo log if needed");
  return cm_undo_log_replay();
}
