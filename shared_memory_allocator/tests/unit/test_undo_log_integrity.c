#include <stdio.h>
#include <string.h>

#include "cm/cm.h"
#include "../../src/internal/faults.h"
#include "../../src/internal/mapping.h"
#include "../../src/internal/metadata.h"
#include "../../src/internal/undo_log.h"

int main(void) {
  cm_open_opts opts;
  cm_status status;
  cm_metadata_header* header;
  uint16_t* entries;
  unsigned char* base;
  unsigned char* log_base;
  size_t page_size;
  size_t baseline_len;

  opts.logical_size = 1u << 20;
  opts.flags = 0;

  status = cm_open("/cm_unit_undo_log_integrity", &opts);
  if (status != CM_OK) {
    fprintf(stderr, "expected CM_OK from cm_open, got %s\n", cm_status_string(status));
    return 1;
  }

  (void)cm_faults_disarm();

  header = cm_metadata_header_ptr();
  entries = cm_metadata_undo_entries();
  base = (unsigned char*)cm_mapping_base();
  page_size = cm_mapping_page_size();
  log_base = base + cm_mapping_logical_size();
  if (header == NULL || entries == NULL || base == NULL || page_size == 0) {
    fprintf(stderr, "failed to access metadata/mapping internals\n");
    cm_close();
    return 1;
  }

  memset(base, 0x3A, page_size);
  memcpy(log_base, base, page_size);
  status = cm_undo_log_append(0);
  if (status != CM_OK) {
    fprintf(stderr, "expected append success, got %s\n", cm_status_string(status));
    cm_close();
    return 1;
  }

  baseline_len = cm_metadata_undo_length_load(header);
  if (baseline_len != 1 || entries[0] != 0u) {
    fprintf(stderr, "unexpected undo log state after append: len=%zu idx0=%u\n", baseline_len,
            (unsigned int)entries[0]);
    cm_close();
    return 1;
  }

  memset(base, 0x7B, page_size);
  status = cm_undo_log_replay();
  if (status != CM_OK) {
    fprintf(stderr, "expected replay success, got %s\n", cm_status_string(status));
    cm_close();
    return 1;
  }
  if (cm_metadata_undo_length_load(header) != 0) {
    fprintf(stderr, "expected undo log length to be 0 after replay\n");
    cm_close();
    return 1;
  }
  if (base[0] != 0x3A || base[page_size - 1] != 0x3A) {
    fprintf(stderr, "replay did not restore baseline bytes\n");
    cm_close();
    return 1;
  }

  memset(base, 0x45, page_size);
  memcpy(log_base, base, page_size);
  status = cm_undo_log_append(0);
  if (status != CM_OK) {
    fprintf(stderr, "expected second append success, got %s\n", cm_status_string(status));
    cm_close();
    return 1;
  }
  memset(base, 0x92, page_size);
  status = cm_undo_log_reset();
  if (status != CM_OK) {
    fprintf(stderr, "expected reset success, got %s\n", cm_status_string(status));
    cm_close();
    return 1;
  }
  if (cm_metadata_undo_length_load(header) != 0) {
    fprintf(stderr, "expected undo log length to be 0 after reset\n");
    cm_close();
    return 1;
  }
  if (base[0] != 0x92) {
    fprintf(stderr, "reset should not modify base bytes\n");
    cm_close();
    return 1;
  }

  status = cm_undo_log_append(header->undo_log_capacity);
  if (status == CM_OK) {
    fprintf(stderr, "expected out-of-range append to fail\n");
    cm_close();
    return 1;
  }

  cm_close();
  return 0;
}
