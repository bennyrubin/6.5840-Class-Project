#ifndef CM_INTERNAL_TRACE_H
#define CM_INTERNAL_TRACE_H

int cm_trace_enabled(void);
void cm_trace_log(const char* component, const char* event, const char* detail);
int cm_trace_crash_point_matches(const char* name);
void cm_trace_maybe_abort(const char* name);

#endif
