#include <stdio.h>
#include <string.h>

#include "cm/cm.h"

static cm_status init_ok(void* ptr, size_t size, void* ctx) {
  (void)ctx;
  memset(ptr, 0x5A, size);
  return CM_OK;
}

static cm_status init_fail(void* ptr, size_t size, void* ctx) {
  (void)ptr;
  (void)size;
  (void)ctx;
  return CM_ERR_IO;
}

int main(void) {
  cm_open_opts opts;
  cm_status status;
  void* ptr = NULL;
  void* ptr2 = NULL;
  char long_name[128];

  memset(long_name, 'a', sizeof(long_name));
  long_name[sizeof(long_name) - 1] = '\0';

  status = cm_get_oralloc(NULL, 8u, init_ok, NULL, &ptr);
  if (status != CM_ERR_INVALID_ARGUMENT) {
    fprintf(stderr, "expected invalid-argument for NULL name, got %s\n", cm_status_string(status));
    return 1;
  }
  status = cm_get_oralloc("", 8u, init_ok, NULL, &ptr);
  if (status != CM_ERR_INVALID_ARGUMENT) {
    fprintf(stderr, "expected invalid-argument for empty name, got %s\n", cm_status_string(status));
    return 1;
  }
  status = cm_get_oralloc("root", 0u, init_ok, NULL, &ptr);
  if (status != CM_ERR_INVALID_ARGUMENT) {
    fprintf(stderr, "expected invalid-argument for zero size, got %s\n", cm_status_string(status));
    return 1;
  }
  status = cm_get_oralloc("root", 8u, init_ok, NULL, NULL);
  if (status != CM_ERR_INVALID_ARGUMENT) {
    fprintf(stderr, "expected invalid-argument for NULL out_ptr, got %s\n",
            cm_status_string(status));
    return 1;
  }
  status = cm_get_oralloc(long_name, 8u, init_ok, NULL, &ptr);
  if (status != CM_ERR_INVALID_ARGUMENT) {
    fprintf(stderr, "expected invalid-argument for oversized name, got %s\n",
            cm_status_string(status));
    return 1;
  }

  status = cm_get_oralloc("root", 8u, init_ok, NULL, &ptr);
  if (status != CM_ERR_IO) {
    fprintf(stderr, "expected I/O error from cm_get_oralloc without cm_open, got %s\n",
            cm_status_string(status));
    return 1;
  }

  opts.logical_size = 1u << 20;
  opts.flags = 0;
  status = cm_open("/cm_unit_named_error_paths", &opts);
  if (status != CM_OK) {
    fprintf(stderr, "expected cm_open success, got %s\n", cm_status_string(status));
    return 1;
  }

  status = cm_get_oralloc("root", 128u, init_ok, NULL, &ptr);
  if (status != CM_OK || ptr == NULL) {
    fprintf(stderr, "expected initial cm_get_oralloc success, got %s\n", cm_status_string(status));
    cm_close();
    return 1;
  }

  status = cm_get_oralloc("root", 128u, NULL, NULL, &ptr2);
  if (status != CM_OK || ptr2 != ptr) {
    fprintf(stderr, "expected ready-entry lookup to succeed without init_fn, got %s\n",
            cm_status_string(status));
    cm_close();
    return 1;
  }

  /* Since named realloc was introduced, get_oralloc no longer rejects size
   * mismatches for existing entries.  A different size on a READY entry is
   * now accepted and the existing pointer is returned. */
  status = cm_get_oralloc("root", 256u, init_ok, NULL, &ptr2);
  if (status != CM_OK || ptr2 != ptr) {
    fprintf(stderr, "expected size-mismatch on existing entry to return CM_OK (new behaviour), got %s\n",
            cm_status_string(status));
    cm_close();
    return 1;
  }

  status = cm_get_oralloc("needs_init", 64u, init_fail, NULL, &ptr2);
  if (status != CM_ERR_IO) {
    fprintf(stderr, "expected init callback failure to propagate CM_ERR_IO, got %s\n",
            cm_status_string(status));
    cm_close();
    return 1;
  }

  status = cm_get_oralloc("needs_init", 64u, NULL, NULL, &ptr2);
  if (status != CM_ERR_INVALID_ARGUMENT) {
    fprintf(stderr, "expected non-ready entry with NULL init_fn to fail, got %s\n",
            cm_status_string(status));
    cm_close();
    return 1;
  }

  status = cm_get_oralloc("needs_init", 64u, init_ok, NULL, &ptr2);
  if (status != CM_OK || ptr2 == NULL) {
    fprintf(stderr, "expected re-init of non-ready entry to succeed, got %s\n",
            cm_status_string(status));
    cm_close();
    return 1;
  }

  cm_close();
  return 0;
}
