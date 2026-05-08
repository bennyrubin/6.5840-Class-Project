#include "internal/commit_policy.h"

#include <stdio.h>
#include <string.h>

#include "internal/debug.h"

#define CM_KNOWN_OPEN_FLAGS \
  (CM_OPEN_F_COMMIT_INTERVAL | CM_OPEN_F_COMMIT_MEMORY_PRESSURE)

typedef struct cm_commit_policy_state {
  int initialized;
  int interval_enabled;
  int memory_pressure_enabled;
  size_t commit_interval;
  uint32_t memory_threshold_percent;
  size_t requested_commits;
  size_t actual_commits;
} cm_commit_policy_state;

static cm_commit_policy_state g_commit_policy;

static cm_status cm_commit_policy_read_memory_used_percent(uint32_t* out_used_percent) {
  FILE* fp;
  char key[64];
  char unit[32];
  unsigned long long value;
  unsigned long long mem_total = 0;
  unsigned long long mem_available = 0;

  if (out_used_percent == NULL) {
    return CM_ERR_INVALID_ARGUMENT;
  }

  fp = fopen("/proc/meminfo", "r");
  if (fp == NULL) {
    return CM_ERR_IO;
  }

  while (fscanf(fp, "%63[^:]: %llu %31s\n", key, &value, unit) == 3) {
    if (strcmp(key, "MemTotal") == 0) {
      mem_total = value;
    } else if (strcmp(key, "MemAvailable") == 0) {
      mem_available = value;
    }
    if (mem_total > 0 && mem_available > 0) {
      break;
    }
  }

  if (fclose(fp) != 0) {
    return CM_ERR_IO;
  }
  if (mem_total == 0 || mem_available > mem_total) {
    return CM_ERR_IO;
  }

  *out_used_percent =
      (uint32_t)(((mem_total - mem_available) * 100ULL + (mem_total - 1ULL)) / mem_total);
  return CM_OK;
}

cm_status cm_commit_policy_init(const cm_open_opts* opts) {
  uint32_t flags;

  if (opts == NULL) {
    return CM_ERR_INVALID_ARGUMENT;
  }

  flags = opts->flags;
  if ((flags & ~CM_KNOWN_OPEN_FLAGS) != 0u) {
    return CM_ERR_INVALID_ARGUMENT;
  }
  if ((flags & CM_OPEN_F_COMMIT_INTERVAL) != 0u && opts->commit_interval == 0) {
    return CM_ERR_INVALID_ARGUMENT;
  }
  if ((flags & CM_OPEN_F_COMMIT_MEMORY_PRESSURE) != 0u &&
      (opts->commit_memory_threshold_percent == 0 ||
       opts->commit_memory_threshold_percent > 100u)) {
    return CM_ERR_INVALID_ARGUMENT;
  }

  memset(&g_commit_policy, 0, sizeof(g_commit_policy));
  g_commit_policy.initialized = 1;
  g_commit_policy.interval_enabled = (flags & CM_OPEN_F_COMMIT_INTERVAL) != 0u;
  g_commit_policy.memory_pressure_enabled = (flags & CM_OPEN_F_COMMIT_MEMORY_PRESSURE) != 0u;
  g_commit_policy.commit_interval = opts->commit_interval;
  g_commit_policy.memory_threshold_percent = opts->commit_memory_threshold_percent;
  return CM_OK;
}

void cm_commit_policy_reset(void) {
  memset(&g_commit_policy, 0, sizeof(g_commit_policy));
}

cm_status cm_commit_policy_before_commit(void) {
  cm_status status;
  uint32_t used_percent = 0;

  if (!g_commit_policy.initialized) {
    return CM_OK;
  }

  g_commit_policy.requested_commits += 1u;

  if (!g_commit_policy.interval_enabled && !g_commit_policy.memory_pressure_enabled) {
    return CM_OK;
  }

  if (g_commit_policy.memory_pressure_enabled) {
    status = cm_commit_policy_read_memory_used_percent(&used_percent);
    if (status != CM_OK) {
      return status;
    }
    if (used_percent >= g_commit_policy.memory_threshold_percent) {
      CM_DLOG("commit_policy", "commit_memory_pressure", "memory pressure threshold reached");
      return CM_OK;
    }
  }

  if (g_commit_policy.interval_enabled) {
    if ((g_commit_policy.requested_commits % g_commit_policy.commit_interval) == 0u) {
      CM_DLOG("commit_policy", "commit_interval", "commit interval reached");
      return CM_OK;
    }
    return CM_COMMIT_SKIPPED_INTERVAL;
  }

  return CM_COMMIT_SKIPPED_MEMORY_BELOW_THRESHOLD;
}

void cm_commit_policy_note_commit_success(void) {
  if (!g_commit_policy.initialized) {
    return;
  }
  g_commit_policy.actual_commits += 1u;
}
