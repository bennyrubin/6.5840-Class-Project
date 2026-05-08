#ifndef CM_INTERNAL_UNDO_LOG_H
#define CM_INTERNAL_UNDO_LOG_H

#include <stddef.h>

#include "cm/cm.h"

cm_status cm_undo_log_append(size_t page_index);
cm_status cm_undo_log_replay(void);
cm_status cm_undo_log_reset(void);

#endif
