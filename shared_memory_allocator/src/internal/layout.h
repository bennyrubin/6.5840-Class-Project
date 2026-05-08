#ifndef CM_INTERNAL_LAYOUT_H
#define CM_INTERNAL_LAYOUT_H

#include <stddef.h>
#include <stdint.h>

#define CM_LAYOUT_MAGIC 0x434d53484d31ULL

size_t cm_align_up(size_t value, size_t alignment);

#endif
