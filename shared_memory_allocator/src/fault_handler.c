#include "internal/faults.h"

#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <ucontext.h>

#include "internal/mapping.h"
#include "internal/trace.h"
#include "internal/undo_log.h"

typedef struct cm_fault_state {
  int initialized;
  void* base;
  size_t logical_size;
  size_t page_size;
  struct sigaction previous_segv_action;
} cm_fault_state;

static cm_fault_state g_fault_state = {
    .initialized = 0,
    .base = NULL,
    .logical_size = 0,
    .page_size = 0,
};

static int cm_fault_addr_in_range(uintptr_t addr) {
  uintptr_t base;
  uintptr_t end;

  if (!g_fault_state.initialized || g_fault_state.base == NULL || g_fault_state.logical_size == 0) {
    return 0;
  }
  base = (uintptr_t)g_fault_state.base;
  end = base + g_fault_state.logical_size;
  return addr >= base && addr < end;
}

static void cm_forward_sigsegv(int signo, siginfo_t* info, void* uctx) {
  if ((g_fault_state.previous_segv_action.sa_flags & SA_SIGINFO) != 0 &&
      g_fault_state.previous_segv_action.sa_sigaction != NULL) {
    g_fault_state.previous_segv_action.sa_sigaction(signo, info, uctx);
    return;
  }

  if (g_fault_state.previous_segv_action.sa_handler == SIG_IGN) {
    return;
  }

  if (g_fault_state.previous_segv_action.sa_handler != NULL &&
      g_fault_state.previous_segv_action.sa_handler != SIG_DFL) {
    g_fault_state.previous_segv_action.sa_handler(signo);
    return;
  }

  signal(signo, SIG_DFL);
  raise(signo);
}

static void cm_sigsegv_handler(int signo, siginfo_t* info, void* uctx) {
  uintptr_t fault_addr;
  uintptr_t page_addr;
  size_t page_index;

  (void)uctx;
  if (signo != SIGSEGV || info == NULL) {
    cm_forward_sigsegv(signo, info, uctx);
    return;
  }

  fault_addr = (uintptr_t)info->si_addr;
  if (!cm_fault_addr_in_range(fault_addr)) {
    cm_forward_sigsegv(signo, info, uctx);
    return;
  }

  page_addr = fault_addr & ~((uintptr_t)g_fault_state.page_size - 1u);
  page_index = (page_addr - (uintptr_t)g_fault_state.base) / g_fault_state.page_size;
  if (cm_faults_handle_first_write(page_index) != CM_OK) {
    abort();
  }
}

cm_status cm_faults_arm(void) {
  struct sigaction action;

  if (!cm_mapping_is_open()) {
    return CM_ERR_IO;
  }

  g_fault_state.base = cm_mapping_base();
  g_fault_state.logical_size = cm_mapping_logical_size();
  g_fault_state.page_size = cm_mapping_page_size();
  if (g_fault_state.base == NULL || g_fault_state.logical_size == 0 || g_fault_state.page_size == 0) {
    return CM_ERR_IO;
  }

  if (!g_fault_state.initialized) {
    memset(&action, 0, sizeof(action));
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO | SA_NODEFER;
    action.sa_sigaction = cm_sigsegv_handler;
    if (sigaction(SIGSEGV, &action, &g_fault_state.previous_segv_action) != 0) {
      return CM_ERR_IO;
    }
    g_fault_state.initialized = 1;
  }

  if (mprotect(g_fault_state.base, g_fault_state.logical_size, PROT_READ) != 0) {
    return CM_ERR_IO;
  }
  return CM_OK;
}

cm_status cm_faults_disarm(void) {
  cm_status status = CM_OK;

  if (!g_fault_state.initialized) {
    return CM_OK;
  }

  if (g_fault_state.base != NULL && g_fault_state.logical_size > 0) {
    if (mprotect(g_fault_state.base, g_fault_state.logical_size, PROT_READ | PROT_WRITE) != 0) {
      status = CM_ERR_IO;
    }
  }
  if (sigaction(SIGSEGV, &g_fault_state.previous_segv_action, NULL) != 0) {
    status = CM_ERR_IO;
  }

  g_fault_state.initialized = 0;
  g_fault_state.base = NULL;
  g_fault_state.logical_size = 0;
  g_fault_state.page_size = 0;
  memset(&g_fault_state.previous_segv_action, 0, sizeof(g_fault_state.previous_segv_action));
  return status;
}

cm_status cm_faults_handle_first_write(size_t page_index) {
  void* base_page;
  void* log_page;
  size_t page_count;
  cm_status status;

  page_count = cm_mapping_logical_size() / cm_mapping_page_size();
  if (cm_mapping_page_size() == 0 || page_index >= page_count) {
    return CM_ERR_INVALID_ARGUMENT;
  }

  base_page = (void*)((char*)cm_mapping_base() + (page_index * cm_mapping_page_size()));
  log_page = (void*)((char*)cm_mapping_base() + cm_mapping_logical_size() +
                     (page_index * cm_mapping_page_size()));

  memcpy(log_page, base_page, cm_mapping_page_size());
  cm_trace_maybe_abort("after_undo_copy_before_append");

  status = cm_undo_log_append(page_index);
  if (status != CM_OK) {
    return status;
  }
  cm_trace_maybe_abort("after_undo_append_before_unprotect");

  if (mprotect(base_page, cm_mapping_page_size(), PROT_READ | PROT_WRITE) != 0) {
    return CM_ERR_IO;
  }
  return CM_OK;
}
