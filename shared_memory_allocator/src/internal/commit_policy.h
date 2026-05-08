#ifndef CM_INTERNAL_COMMIT_POLICY_H
#define CM_INTERNAL_COMMIT_POLICY_H

#include "cm/cm.h"

cm_status cm_commit_policy_init(const cm_open_opts* opts);
void cm_commit_policy_reset(void);
cm_status cm_commit_policy_before_commit(void);
void cm_commit_policy_note_commit_success(void);

#endif
