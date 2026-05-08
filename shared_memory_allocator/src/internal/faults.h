#ifndef CM_INTERNAL_FAULTS_H
#define CM_INTERNAL_FAULTS_H

#include "cm/cm.h"

cm_status cm_faults_arm(void);
cm_status cm_faults_disarm(void);
cm_status cm_faults_handle_first_write(size_t page_index);

#endif
