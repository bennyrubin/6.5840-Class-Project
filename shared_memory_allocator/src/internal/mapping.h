#ifndef CM_INTERNAL_MAPPING_H
#define CM_INTERNAL_MAPPING_H

#include <stddef.h>

#include "cm/cm.h"

cm_status cm_mapping_open(const char* shm_name, const cm_open_opts* opts);
cm_status cm_mapping_close(void);
int cm_mapping_is_open(void);
int cm_mapping_is_fresh(void);
size_t cm_mapping_page_size(void);
size_t cm_mapping_logical_size(void);
size_t cm_mapping_total_size(void);
size_t cm_mapping_metadata_size(void);
void* cm_mapping_base(void);
void* cm_mapping_metadata_base(void);

#endif
