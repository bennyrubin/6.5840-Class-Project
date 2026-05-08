#include "cm/cm_inspect.h"

#include <stdlib.h>

struct cm_inspect_handle {
  int reserved;
};

cm_status cm_inspect_open(const char* shm_name, cm_inspect_handle** out_handle) {
  cm_inspect_handle* handle;

  if (shm_name == NULL || out_handle == NULL) {
    return CM_ERR_INVALID_ARGUMENT;
  }

  handle = (cm_inspect_handle*)calloc(1, sizeof(*handle));
  if (handle == NULL) {
    return CM_ERR_IO;
  }

  *out_handle = handle;
  return CM_ERR_NOT_IMPLEMENTED;
}

cm_status cm_inspect_read_layout(cm_inspect_handle* handle, cm_shm_layout_info* out_info) {
  (void)handle;
  (void)out_info;
  return CM_ERR_NOT_IMPLEMENTED;
}

cm_status cm_inspect_dump(cm_inspect_handle* handle, FILE* out_stream) {
  (void)handle;
  (void)out_stream;
  return CM_ERR_NOT_IMPLEMENTED;
}

void cm_inspect_close(cm_inspect_handle* handle) {
  if (handle == NULL) {
    return;
  }
  free(handle);
}
