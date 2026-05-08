#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cm/cm.h"
#include "test_helpers.h"

static int g_reinit_calls = 0;

static cm_status init_maybe_crash(void* ptr, size_t size, void* ctx) {
  const char* crash = getenv("CM_CRASH_POINT");
  (void)ctx;
  if (crash != NULL && strcmp(crash, "named_init_before_write") == 0) {
    abort();
  }
  memset(ptr, 0xA7, size);
  return CM_OK;
}

static cm_status init_count_calls(void* ptr, size_t size, void* ctx) {
  (void)ptr;
  (void)size;
  (void)ctx;
  g_reinit_calls += 1;
  return CM_OK;
}

static int run_writer(const char* shm_name) {
  void* ptr = NULL;
  cm_status status;

  if (th_open_default(shm_name, 1u << 20) != 0) {
    fprintf(stderr, "writer: cm_open failed\n");
    return 2;
  }

  status = cm_get_oralloc("root", 4096u, init_maybe_crash, NULL, &ptr);
  if (status != CM_OK) {
    fprintf(stderr, "writer: cm_get_oralloc failed: %s\n", cm_status_string(status));
    cm_close();
    return 3;
  }
  cm_close();
  return 0;
}

int main(int argc, char** argv) {
  const char* shm_name = "/cm_integration_named_lifecycle";
  int exit_code;
  int term_sig;
  void* ptr = NULL;
  cm_status status;

  if (argc >= 3 && strcmp(argv[1], "writer") == 0) {
    return run_writer(argv[2]);
  }

  th_cleanup_shm(shm_name);
  if (th_spawn_mode(
          argv[0], "writer", shm_name, "named_init_before_write", &exit_code, &term_sig) != 0) {
    fprintf(stderr, "failed to spawn named writer\n");
    return 1;
  }
  if (term_sig != SIGABRT) {
    fprintf(stderr, "expected SIGABRT in named writer, got exit=%d sig=%d\n", exit_code, term_sig);
    th_cleanup_shm(shm_name);
    return 1;
  }

  if (th_open_default(shm_name, 1u << 20) != 0) {
    fprintf(stderr, "reopen after named init crash failed\n");
    th_cleanup_shm(shm_name);
    return 1;
  }

  status = cm_get_oralloc("root", 4096u, init_maybe_crash, NULL, &ptr);
  if (status != CM_OK) {
    fprintf(stderr, "recovery cm_get_oralloc failed: %s\n", cm_status_string(status));
    cm_close();
    return 1;
  }
  if (((unsigned char*)ptr)[0] != 0xA7u) {
    fprintf(stderr, "expected initialized bytes after recovery path\n");
    cm_close();
    return 1;
  }

  g_reinit_calls = 0;
  status = cm_get_oralloc("root", 4096u, init_count_calls, NULL, &ptr);
  if (status != CM_OK) {
    fprintf(stderr, "second cm_get_oralloc failed: %s\n", cm_status_string(status));
    cm_close();
    return 1;
  }
  if (g_reinit_calls != 0) {
    fprintf(stderr, "init callback should not run for ready objects, calls=%d\n", g_reinit_calls);
    cm_close();
    return 1;
  }

  cm_close();
  return 0;
}
