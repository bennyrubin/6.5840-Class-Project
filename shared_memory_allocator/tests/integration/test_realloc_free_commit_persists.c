#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cm/cm.h"
#include "test_helpers.h"
#include "../../src/internal/mapping.h"

typedef struct root_state {
  size_t data_offset;
  size_t data_size;
  size_t freed_offset;
} root_state;

static cm_status init_root(void* ptr, size_t size, void* ctx) {
  (void)ctx;
  memset(ptr, 0, size);
  return CM_OK;
}

static int run_writer_then_crash(const char* shm_name) {
  root_state* root;
  unsigned char* data;
  unsigned char* tmp;
  unsigned char* base;
  cm_status status;

  if (th_open_default(shm_name, 1u << 20) != 0) {
    fprintf(stderr, "writer: cm_open failed\n");
    return 2;
  }

  root = NULL;
  status = cm_get_oralloc("root", sizeof(*root), init_root, NULL, (void**)&root);
  if (status != CM_OK || root == NULL) {
    fprintf(stderr, "writer: cm_get_oralloc(root) failed: %s\n", cm_status_string(status));
    cm_close();
    return 3;
  }

  data = (unsigned char*)cm_alloc(96);
  if (data == NULL) {
    fprintf(stderr, "writer: initial alloc failed\n");
    cm_close();
    return 4;
  }
  th_fill_pattern(data, 96u, 0x55u);

  data = (unsigned char*)cm_realloc(data, 512u);
  if (data == NULL) {
    fprintf(stderr, "writer: realloc grow failed\n");
    cm_close();
    return 5;
  }
  th_fill_pattern(data, 512u, 0xA1u);

  tmp = (unsigned char*)cm_alloc(128u);
  if (tmp == NULL) {
    fprintf(stderr, "writer: temp alloc failed\n");
    cm_close();
    return 6;
  }

  base = (unsigned char*)cm_mapping_base();
  root->data_offset = (size_t)(data - base);
  root->data_size = 512u;
  root->freed_offset = (size_t)(tmp - base);

  cm_free(tmp);
  if (cm_commit() != CM_OK) {
    fprintf(stderr, "writer: cm_commit failed\n");
    cm_close();
    return 7;
  }

  abort();
  return 0;
}

int main(int argc, char** argv) {
  const char* shm_name = "/cm_integration_realloc_free_commit_persists";
  int exit_code;
  int term_sig;
  root_state* root;
  cm_status status;
  unsigned char* base;
  unsigned char* probe;

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

  if (th_open_default(shm_name, 1u << 20) != 0) {
    fprintf(stderr, "reopen after crash failed\n");
    th_cleanup_shm(shm_name);
    return 1;
  }

  root = NULL;
  status = cm_get_oralloc("root", sizeof(*root), init_root, NULL, (void**)&root);
  if (status != CM_OK || root == NULL) {
    fprintf(stderr, "reopen: cm_get_oralloc(root) failed: %s\n", cm_status_string(status));
    cm_close();
    return 1;
  }
  if (root->data_size != 512u) {
    fprintf(stderr, "expected committed root size 512, got %zu\n", root->data_size);
    cm_close();
    return 1;
  }

  base = (unsigned char*)cm_mapping_base();
  if (th_expect_pattern(base + root->data_offset, root->data_size, 0xA1u) != 0) {
    fprintf(stderr, "committed realloc data did not persist across crash\n");
    cm_close();
    return 1;
  }

  probe = (unsigned char*)cm_alloc(64u);
  if (probe == NULL) {
    fprintf(stderr, "expected allocation from persisted free-list entry\n");
    cm_close();
    return 1;
  }
  if ((size_t)(probe - base) != root->freed_offset) {
    fprintf(stderr, "expected committed free-list block reuse at offset %zu, got %zu\n",
            root->freed_offset, (size_t)(probe - base));
    cm_close();
    return 1;
  }

  cm_close();
  return 0;
}
