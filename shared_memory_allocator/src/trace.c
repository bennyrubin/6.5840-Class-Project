#include "internal/trace.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

int cm_trace_enabled(void) {
  static int initialized = 0;
  static int enabled = 0;
  const char* trace_env;

  if (initialized) {
    return enabled;
  }

  trace_env = getenv("CM_TRACE");
  if (trace_env != NULL && trace_env[0] != '\0' && trace_env[0] != '0') {
    enabled = 1;
  }

  initialized = 1;
  return enabled;
}

void cm_trace_log(const char* component, const char* event, const char* detail) {
  const char* c = component != NULL ? component : "unknown";
  const char* e = event != NULL ? event : "unknown";
  const char* d = detail != NULL ? detail : "";
  if (!cm_trace_enabled()) {
    return;
  }
  fprintf(stderr, "[cm][%s][%s] %s\n", c, e, d);
}

int cm_trace_crash_point_matches(const char* name) {
  const char* target;

  if (name == NULL || name[0] == '\0') {
    return 0;
  }
  target = getenv("CM_CRASH_POINT");
  if (target == NULL || target[0] == '\0') {
    return 0;
  }
  return strcmp(target, name) == 0;
}

void cm_trace_maybe_abort(const char* name) {
  if (!cm_trace_crash_point_matches(name)) {
    return;
  }
  fprintf(stderr, "[cm][crash_point] abort at %s\n", name);
  abort();
}
