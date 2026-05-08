#ifndef CM_H
#define CM_H

#include <stddef.h>

#include "cm/cm_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef cm_status (*cm_init_fn)(void* ptr, size_t size, void* ctx);

cm_status cm_open(const char* shm_name, const cm_open_opts* opts);

/*
 * Close process-local resources and unlink the backing shared-memory object.
 * After this call, reopening the same name creates a fresh object.
 */
void cm_close(void);

void* cm_alloc(size_t size);
void cm_free(void* ptr);
void* cm_realloc(void* ptr, size_t size);
cm_status cm_get_oralloc(
    const char* name,
    size_t size,
    cm_init_fn init_fn,
    void* init_ctx,
    void** out_ptr);

cm_status cm_commit(void);
int cm_status_is_error(cm_status status);
int cm_commit_status_did_commit(cm_status status);
int cm_commit_status_was_skipped(cm_status status);
const char* cm_status_string(cm_status status);

#ifdef __cplusplus
}
#endif

#endif
