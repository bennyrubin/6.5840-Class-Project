#include <stdio.h>

#include "cm/cm_inspect.h"

int main(int argc, char** argv) {
  cm_inspect_handle* inspect = NULL;
  cm_status status;

  if (argc < 2) {
    fprintf(stderr, "usage: %s <shm_name>\n", argv[0]);
    return 2;
  }

  status = cm_inspect_open(argv[1], &inspect);
  if (status != CM_OK) {
    fprintf(stderr, "cm_inspect_open failed: %d\n", (int)status);
    return 2;
  }

  status = cm_inspect_dump(inspect, stdout);
  if (status != CM_OK) {
    fprintf(stderr, "cm_inspect_dump failed: %d\n", (int)status);
    cm_inspect_close(inspect);
    return 2;
  }

  cm_inspect_close(inspect);
  return 2;
}
