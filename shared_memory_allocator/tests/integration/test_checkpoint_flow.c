#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cm/cm.h"

static void checkpoint(const char* name) {
  const char* target = getenv("CM_CRASH_POINT");
  if (target != NULL && name != NULL && target[0] != '\0' && strcmp(target, name) == 0) {
    fprintf(stderr, "checkpoint hit: %s\n", name);
    abort();
  }
}

int main(void) {
  cm_open_opts opts;
  cm_status status;

  opts.logical_size = 2u << 20;
  opts.flags = 0;

  status = cm_open("/cm_integration_checkpoint", &opts);
  if (status != CM_OK) {
    fprintf(stderr, "checkpoint flow expected cm_open success, got %s\n", cm_status_string(status));
    return 1;
  }

  checkpoint("after_open");

  status = cm_commit();
  if (status != CM_OK) {
    fprintf(stderr, "checkpoint flow expected cm_commit success, got %s\n", cm_status_string(status));
    cm_close();
    return 1;
  }

  checkpoint("after_commit");

  cm_close();
  return 0;
}
