#include "cm/cm.h"

#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "internal/allocator.h"
#include "internal/commit.h"
#include "internal/commit_policy.h"
#include "internal/debug.h"
#include "internal/faults.h"
#include "internal/mapping.h"
#include "internal/metadata.h"
#include "internal/recovery.h"

typedef struct cm_runtime_state {
  char* shm_name;
} cm_runtime_state;

static cm_runtime_state g_cm_runtime;

cm_status cm_open(const char* shm_name, const cm_open_opts* opts) {
  cm_status status;
  char* name_copy;
  int unlink_on_fail;

  CM_DLOG("api", "open_begin", "cm_open called");
  if (shm_name == NULL || opts == NULL) {
    return CM_ERR_INVALID_ARGUMENT;
  }
  if (opts->logical_size == 0) {
    return CM_ERR_INVALID_ARGUMENT;
  }
  if (g_cm_runtime.shm_name != NULL) {
    return CM_ERR_INVALID_ARGUMENT;
  }

  status = cm_commit_policy_init(opts);
  if (status != CM_OK) {
    return status;
  }

  status = cm_mapping_open(shm_name, opts);
  if (status != CM_OK) {
    CM_DLOG("api", "open_fail_mapping", "cm_mapping_open failed");
    cm_commit_policy_reset();
    return status;
  }

  status = cm_metadata_init();
  if (status != CM_OK) {
    CM_DLOG("api", "open_fail_metadata_init", "cm_metadata_init failed");
    goto fail_open;
  }

  status = cm_metadata_validate();
  if (status != CM_OK) {
    CM_DLOG("api", "open_fail_metadata_validate", "cm_metadata_validate failed");
    goto fail_open;
  }

  status = cm_recover_if_needed();
  if (status != CM_OK) {
    CM_DLOG("api", "open_fail_recover", "cm_recover_if_needed failed");
    goto fail_open;
  }

  status = cm_faults_arm();
  if (status != CM_OK) {
    CM_DLOG("api", "open_fail_faults_arm", "cm_faults_arm failed");
    goto fail_open;
  }

  name_copy = strdup(shm_name);
  if (name_copy == NULL) {
    status = CM_ERR_IO;
    CM_DLOG("api", "open_fail_strdup", "strdup failed");
    goto fail_open;
  }
  g_cm_runtime.shm_name = name_copy;

  CM_DLOG("api", "open_end", "cm_open completed");
  return CM_OK;

fail_open:
  (void)cm_faults_disarm();
  cm_commit_policy_reset();
  unlink_on_fail = cm_mapping_is_fresh();
  (void)cm_mapping_close();
  if (unlink_on_fail) {
    (void)shm_unlink(shm_name);
  }
  return status;
}

void cm_close(void) {
  CM_DLOG("api", "close_begin", "cm_close called");
  (void)cm_faults_disarm();
  (void)cm_mapping_close();
  cm_commit_policy_reset();
  if (g_cm_runtime.shm_name != NULL) {
    (void)shm_unlink(g_cm_runtime.shm_name);
    free(g_cm_runtime.shm_name);
    g_cm_runtime.shm_name = NULL;
  }
}

void* cm_alloc(size_t size) {
  if (size == 0) {
    return NULL;
  }
  return cm_allocator_alloc(size);
}

void cm_free(void* ptr) {
  cm_allocator_free(ptr);
}

void* cm_realloc(void* ptr, size_t size) {
  return cm_allocator_realloc(ptr, size);
}

cm_status cm_get_oralloc(
    const char* name,
    size_t size,
    cm_init_fn init_fn,
    void* init_ctx,
    void** out_ptr) {
  if (name == NULL || size == 0 || out_ptr == NULL) {
    return CM_ERR_INVALID_ARGUMENT;
  }
  return cm_allocator_get_oralloc(name, size, init_fn, init_ctx, out_ptr);
}

cm_status cm_commit(void) {
  cm_status status;

  CM_DLOG("api", "commit_begin", "cm_commit called");
  status = cm_commit_policy_before_commit();
  if (status != CM_OK) {
    return status;
  }
  status = cm_commit_internal();
  if (status == CM_OK) {
    cm_commit_policy_note_commit_success();
  }
  return status;
}

int cm_status_is_error(cm_status status) {
  return status != CM_OK && !cm_commit_status_was_skipped(status);
}

int cm_commit_status_did_commit(cm_status status) {
  return status == CM_OK;
}

int cm_commit_status_was_skipped(cm_status status) {
  return status == CM_COMMIT_SKIPPED_INTERVAL ||
         status == CM_COMMIT_SKIPPED_MEMORY_BELOW_THRESHOLD;
}

const char* cm_status_string(cm_status status) {
  switch (status) {
    case CM_OK:
      return "CM_OK";
    case CM_ERR_INVALID_ARGUMENT:
      return "CM_ERR_INVALID_ARGUMENT";
    case CM_ERR_IO:
      return "CM_ERR_IO";
    case CM_ERR_CORRUPT_METADATA:
      return "CM_ERR_CORRUPT_METADATA";
    case CM_COMMIT_SKIPPED_INTERVAL:
      return "CM_COMMIT_SKIPPED_INTERVAL";
    case CM_COMMIT_SKIPPED_MEMORY_BELOW_THRESHOLD:
      return "CM_COMMIT_SKIPPED_MEMORY_BELOW_THRESHOLD";
    case CM_ERR_NOT_IMPLEMENTED:
      return "CM_ERR_NOT_IMPLEMENTED";
    default:
      return "CM_ERR_UNKNOWN";
  }
}
