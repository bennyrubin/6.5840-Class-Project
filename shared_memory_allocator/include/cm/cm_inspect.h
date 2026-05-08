#ifndef CM_INSPECT_H
#define CM_INSPECT_H

#include <stddef.h>
#include <stdio.h>

#include "cm/cm_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cm_inspect_handle cm_inspect_handle;

typedef struct cm_shm_layout_info {
  unsigned long long magic;
  unsigned int version;
  unsigned int page_size;
  size_t logical_size;
  size_t base_region_size;
  size_t log_region_size;
  size_t metadata_region_size;
  size_t undo_log_length;
  size_t undo_log_capacity;
} cm_shm_layout_info;

cm_status cm_inspect_open(const char* shm_name, cm_inspect_handle** out_handle);
cm_status cm_inspect_read_layout(cm_inspect_handle* handle, cm_shm_layout_info* out_info);
cm_status cm_inspect_dump(cm_inspect_handle* handle, FILE* out_stream);
void cm_inspect_close(cm_inspect_handle* handle);

#ifdef __cplusplus
}
#endif

#endif
