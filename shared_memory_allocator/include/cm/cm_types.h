#ifndef CM_TYPES_H
#define CM_TYPES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum cm_status {
  CM_OK = 0,
  CM_ERR_INVALID_ARGUMENT = 1,
  CM_ERR_IO = 2,
  CM_ERR_CORRUPT_METADATA = 3,
  CM_COMMIT_SKIPPED_INTERVAL = 100,
  CM_COMMIT_SKIPPED_MEMORY_BELOW_THRESHOLD = 101,
  CM_ERR_NOT_IMPLEMENTED = 1000,
} cm_status;

#define CM_OPEN_F_COMMIT_INTERVAL (1u << 0)
#define CM_OPEN_F_COMMIT_MEMORY_PRESSURE (1u << 1)

typedef enum cm_named_state {
  CM_NAMED_ALLOCATING = 1,
  CM_NAMED_INITIALIZING = 2,
  CM_NAMED_READY = 3,
} cm_named_state;

typedef struct cm_open_opts {
  size_t logical_size;
  uint32_t flags;
  size_t commit_interval;
  uint32_t commit_memory_threshold_percent;
} cm_open_opts;

#ifdef __cplusplus
}
#endif

#endif
