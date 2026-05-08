#include <stdio.h>

#include "cm/cm.h"

int main(void) {
  cm_status status;

  status = cm_commit();
  if (status != CM_ERR_IO) {
    fprintf(stderr, "expected CM_ERR_IO from cm_commit without cm_open, got %s\n",
            cm_status_string(status));
    return 1;
  }

  cm_close();
  return 0;
}
