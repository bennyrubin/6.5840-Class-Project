#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cm/cm.h"
#include "test_helpers.h"
#include "../../src/internal/mapping.h"

static int run_writer_then_crash(const char* shm_name) {
  unsigned char* base;
  size_t page_size;
  size_t pages_to_touch = 16u;
  size_t i;

  if (th_open_default(shm_name, 4u << 20) != 0) {
    fprintf(stderr, "writer: cm_open failed\n");
    return 2;
  }

  base = (unsigned char*)cm_mapping_base();
  page_size = cm_mapping_page_size();
  for (i = 0; i < pages_to_touch; ++i) {
    th_fill_pattern(base + (i * page_size), page_size, (unsigned char)(0x11u + i));
  }
  if (cm_commit() != CM_OK) {
    fprintf(stderr, "writer: initial cm_commit failed\n");
    cm_close();
    return 3;
  }

  for (i = 0; i < pages_to_touch; ++i) {
    th_fill_pattern(base + (i * page_size), page_size, (unsigned char)(0x91u + i));
  }
  abort();
  return 0;
}

int main(int argc, char** argv) {
  const char* shm_name = "/cm_integration_multi_page_cow";
  int exit_code;
  int term_sig;
  unsigned char* base;
  size_t page_size;
  size_t pages_to_touch = 16u;
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
    fprintf(stderr, "expected writer crash with SIGABRT, got exit=%d sig=%d\n", exit_code, term_sig);
    th_cleanup_shm(shm_name);
    return 1;
  }

  if (th_open_default(shm_name, 4u << 20) != 0) {
    fprintf(stderr, "reopen after crash failed\n");
    th_cleanup_shm(shm_name);
    return 1;
  }

  base = (unsigned char*)cm_mapping_base();
  page_size = cm_mapping_page_size();
  for (i = 0; i < pages_to_touch; ++i) {
    if (th_expect_pattern(base + (i * page_size), page_size, (unsigned char)(0x11u + i)) != 0) {
      fprintf(stderr, "rollback mismatch on page %zu\n", i);
      cm_close();
      return 1;
    }
  }

  cm_close();
  return 0;
}
