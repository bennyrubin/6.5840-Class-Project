#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cm/cm.h"
#include "test_helpers.h"

static cm_status init_zeroed(void* ptr, size_t size, void* ctx) {
  (void)ctx;
  memset(ptr, 0, size);
  return CM_OK;
}

static int run_writer_then_crash(const char* shm_name) {
  cm_status status;
  unsigned char* ptr = NULL;
  unsigned char* new_ptr = NULL;
  size_t i;

  if (th_open_default(shm_name, 1u << 20) != 0) {
    fprintf(stderr, "writer: cm_open failed\n");
    return 2;
  }

  status = cm_get_oralloc("model_buf", 64u, init_zeroed, NULL, (void**)&ptr);
  if (status != CM_OK || ptr == NULL) {
    fprintf(stderr, "writer: cm_get_oralloc(model_buf, 64) failed: %s\n",
            cm_status_string(status));
    cm_close();
    return 3;
  }

  for (i = 0; i < 64u; ++i) {
    ptr[i] = (unsigned char)(i % 256u);
  }

  new_ptr = (unsigned char*)cm_realloc(ptr, 256u);
  if (new_ptr == NULL) {
    fprintf(stderr, "writer: cm_realloc(model_buf, 256) failed\n");
    cm_close();
    return 4;
  }

  for (i = 64u; i < 256u; ++i) {
    new_ptr[i] = (unsigned char)(i % 256u);
  }

  if (cm_commit() != CM_OK) {
    fprintf(stderr, "writer: cm_commit failed\n");
    cm_close();
    return 5;
  }

  abort();
  return 0;
}

int main(int argc, char** argv) {
  const char* shm_name = "/cm_named_realloc_persist_test";
  int exit_code;
  int term_sig;
  cm_status status;
  unsigned char* ptr = NULL;
  size_t i;

  if (argc >= 3 && strcmp(argv[1], "writer") == 0) {
    return run_writer_then_crash(argv[2]);
  }

  th_cleanup_shm(shm_name);
  if (th_spawn_mode(argv[0], "writer", shm_name, NULL, &exit_code, &term_sig) != 0) {
    fprintf(stderr, "failed to spawn writer process\n");
    return 1;
  }
  if (term_sig != SIGABRT) {
    fprintf(stderr, "expected writer crash with SIGABRT, got exit=%d sig=%d\n",
            exit_code, term_sig);
    th_cleanup_shm(shm_name);
    return 1;
  }

  if (th_open_default(shm_name, 1u << 20) != 0) {
    fprintf(stderr, "verifier: reopen after crash failed\n");
    th_cleanup_shm(shm_name);
    return 1;
  }

  status = cm_get_oralloc("model_buf", 256u, NULL, NULL, (void**)&ptr);
  if (status != CM_OK || ptr == NULL) {
    fprintf(stderr, "verifier: cm_get_oralloc(model_buf, 256) failed: %s\n",
            cm_status_string(status));
    cm_close();
    th_cleanup_shm(shm_name);
    return 1;
  }

  for (i = 0; i < 256u; ++i) {
    if (ptr[i] != (unsigned char)(i % 256u)) {
      fprintf(stderr,
              "verifier: byte %zu mismatch: expected 0x%02x got 0x%02x\n",
              i, (unsigned)(i % 256u), (unsigned)ptr[i]);
      cm_close();
      th_cleanup_shm(shm_name);
      return 1;
    }
  }

  cm_close();
  th_cleanup_shm(shm_name);
  return 0;
}
