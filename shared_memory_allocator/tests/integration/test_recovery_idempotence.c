#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cm/cm.h"
#include "test_helpers.h"
#include "../../src/internal/mapping.h"
#include "../../src/internal/metadata.h"

static int run_prepare(const char* shm_name) {
  unsigned char* base;
  const char* crash_point_env;
  char crash_point_saved[128];
  int have_crash_point = 0;

  if (th_open_default(shm_name, 1u << 20) != 0) {
    fprintf(stderr, "prepare: cm_open failed\n");
    return 2;
  }
  crash_point_env = getenv("CM_CRASH_POINT");
  if (crash_point_env != NULL && crash_point_env[0] != '\0') {
    size_t n = strlen(crash_point_env);
    if (n >= sizeof(crash_point_saved)) {
      n = sizeof(crash_point_saved) - 1u;
    }
    memcpy(crash_point_saved, crash_point_env, n);
    crash_point_saved[n] = '\0';
    have_crash_point = 1;
    (void)unsetenv("CM_CRASH_POINT");
  }

  base = (unsigned char*)cm_mapping_base();
  th_fill_pattern(base, cm_mapping_page_size(), 0x33u);
  if (cm_commit() != CM_OK) {
    fprintf(stderr, "prepare: initial cm_commit failed\n");
    cm_close();
    return 3;
  }
  if (have_crash_point) {
    (void)setenv("CM_CRASH_POINT", crash_point_saved, 1);
  }

  base[0] = 0xEF;
  abort();
  return 0;
}

static int run_open_only(const char* shm_name) {
  if (th_open_default(shm_name, 1u << 20) != 0) {
    fprintf(stderr, "open-only: cm_open failed\n");
    return 2;
  }
  cm_close();
  return 0;
}

int main(int argc, char** argv) {
  const char* shm_name = "/cm_integration_recovery_idempotence";
  int exit_code;
  int term_sig;
  unsigned char* base;
  cm_metadata_header* header;

  if (argc >= 3 && strcmp(argv[1], "prepare") == 0) {
    return run_prepare(argv[2]);
  }
  if (argc >= 3 && strcmp(argv[1], "open-only") == 0) {
    return run_open_only(argv[2]);
  }

  th_cleanup_shm(shm_name);
  if (th_spawn_mode(
          argv[0], "prepare", shm_name, "after_undo_append_before_unprotect", &exit_code, &term_sig) !=
      0) {
    fprintf(stderr, "failed to spawn prepare process\n");
    return 1;
  }
  if (term_sig != SIGABRT) {
    fprintf(stderr, "expected prepare crash with SIGABRT, got exit=%d sig=%d\n", exit_code, term_sig);
    th_cleanup_shm(shm_name);
    return 1;
  }

  if (th_spawn_mode(
          argv[0],
          "open-only",
          shm_name,
          "during_recovery_after_copy_before_pop",
          &exit_code,
          &term_sig) != 0) {
    fprintf(stderr, "failed to spawn recovery-crash process\n");
    th_cleanup_shm(shm_name);
    return 1;
  }
  if (term_sig != SIGABRT) {
    fprintf(stderr, "expected recovery crash with SIGABRT, got exit=%d sig=%d\n", exit_code, term_sig);
    th_cleanup_shm(shm_name);
    return 1;
  }

  if (th_open_default(shm_name, 1u << 20) != 0) {
    fprintf(stderr, "final reopen after recovery crash failed\n");
    th_cleanup_shm(shm_name);
    return 1;
  }
  base = (unsigned char*)cm_mapping_base();
  if (th_expect_pattern(base, cm_mapping_page_size(), 0x33u) != 0) {
    fprintf(stderr, "idempotence check failed: final page content mismatch\n");
    cm_close();
    return 1;
  }
  header = cm_metadata_header_ptr();
  if (header == NULL) {
    fprintf(stderr, "failed to locate in-process metadata header\n");
    cm_close();
    return 1;
  }
  if (cm_metadata_undo_length_load(header) != 0) {
    fprintf(stderr, "expected undo_log_length == 0 after recovery completion\n");
    cm_close();
    return 1;
  }
  cm_close();
  return 0;
}
