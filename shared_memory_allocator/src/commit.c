#include "internal/commit.h"

#include <stdatomic.h>

#include "internal/debug.h"
#include "internal/faults.h"
#include "internal/trace.h"
#include "internal/undo_log.h"

cm_status cm_commit_internal(void) {
  cm_status status;

  CM_DLOG("commit", "begin", "reset undo log and arm faults");
  atomic_thread_fence(memory_order_seq_cst);
  status = cm_undo_log_reset();
  if (status != CM_OK) {
    return status;
  }
  cm_trace_maybe_abort("during_commit_after_reset_before_reprotect");
  return cm_faults_arm();
}
