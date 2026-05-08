#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cm/cm.h"
#include "test_helpers.h"
#include "../../src/internal/mapping.h"

static int run_writer(const char* shm_name) {
  unsigned char* base;
  const char* crash_point_env;
  char crash_point_saved[128];
  int have_crash_point = 0;

  if (th_open_default(shm_name, 1u << 20) != 0) {
    fprintf(stderr, "writer: cm_open failed\n");
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
  base[0] = 0x11;
  if (cm_commit() != CM_OK) {
    fprintf(stderr, "writer: initial cm_commit failed\n");
    cm_close();
    return 3;
  }
  if (have_crash_point) {
    (void)setenv("CM_CRASH_POINT", crash_point_saved, 1);
  }

  base[0] = 0x22;
  if (cm_commit() != CM_OK) {
    fprintf(stderr, "writer: second cm_commit failed\n");
    cm_close();
    return 4;
  }

  cm_close();
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

static int assert_restart_value(const char* shm_name, unsigned char expected) {
  unsigned char* base;

  if (th_open_default(shm_name, 1u << 20) != 0) {
    fprintf(stderr, "restart open failed for %s\n", shm_name);
    return -1;
  }
  base = (unsigned char*)cm_mapping_base();
  if (base[0] != expected) {
    fprintf(stderr, "restart mismatch: expected 0x%02x got 0x%02x\n", expected, base[0]);
    cm_close();
    return -1;
  }
  cm_close();
  return 0;
}

int main(int argc, char** argv) {
  const char* shm_name = "/cm_integration_crash_matrix";
  struct matrix_case {
    const char* crash_point;
    unsigned char expected_after_restart;
  } cases[] = {
      {"after_undo_copy_before_append", 0x11u},
      {"after_undo_append_before_unprotect", 0x11u},
      {"during_commit_after_reset_before_reprotect", 0x22u},
  };
  size_t i;

  if (argc >= 3 && strcmp(argv[1], "writer") == 0) {
    return run_writer(argv[2]);
  }
  if (argc >= 3 && strcmp(argv[1], "open-only") == 0) {
    return run_open_only(argv[2]);
  }

  for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    int exit_code;
    int term_sig;

    th_cleanup_shm(shm_name);
    if (th_spawn_mode(
            argv[0], "writer", shm_name, cases[i].crash_point, &exit_code, &term_sig) != 0) {
      fprintf(stderr, "failed to spawn crash-matrix writer for %s\n", cases[i].crash_point);
      return 1;
    }
    if (term_sig != SIGABRT) {
      fprintf(stderr, "expected SIGABRT at %s, got exit=%d sig=%d\n", cases[i].crash_point,
              exit_code, term_sig);
      th_cleanup_shm(shm_name);
      return 1;
    }
    if (assert_restart_value(shm_name, cases[i].expected_after_restart) != 0) {
      th_cleanup_shm(shm_name);
      return 1;
    }
  }

  {
    int exit_code;
    int term_sig;

    th_cleanup_shm(shm_name);
    if (th_spawn_mode(
            argv[0],
            "writer",
            shm_name,
            "after_undo_append_before_unprotect",
            &exit_code,
            &term_sig) != 0) {
      fprintf(stderr, "failed to create undo state for recovery-crash case\n");
      return 1;
    }
    if (term_sig != SIGABRT) {
      fprintf(stderr, "expected initial crash before recovery, got exit=%d sig=%d\n", exit_code,
              term_sig);
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
      fprintf(stderr, "failed to spawn recovery-crash opener\n");
      th_cleanup_shm(shm_name);
      return 1;
    }
    if (term_sig != SIGABRT) {
      fprintf(stderr, "expected SIGABRT during recovery, got exit=%d sig=%d\n", exit_code, term_sig);
      th_cleanup_shm(shm_name);
      return 1;
    }

    if (assert_restart_value(shm_name, 0x11u) != 0) {
      th_cleanup_shm(shm_name);
      return 1;
    }
  }

  return 0;
}
