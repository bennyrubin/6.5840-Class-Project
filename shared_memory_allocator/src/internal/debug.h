#ifndef CM_INTERNAL_DEBUG_H
#define CM_INTERNAL_DEBUG_H

#include <assert.h>

#include "internal/trace.h"

#ifdef CM_DEBUG
#define CM_DASSERT(expr) assert((expr))
#define CM_DLOG(component, event, detail) cm_trace_log((component), (event), (detail))
#else
#define CM_DASSERT(expr) ((void)0)
#define CM_DLOG(component, event, detail) ((void)0)
#endif

#endif
