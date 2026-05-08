#define _POSIX_C_SOURCE 200809L

#include "kmeans.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "cm/cm.h"

#define KM_DEFAULT_SHM_NAME "/cm_kmeans_app"
#define KM_INIT_SEED 42u

typedef km_status (*km_commit_fn)(void* ctx);

typedef struct km_init_ctx {
  const km_point* points;
  size_t n_points;
  int k;
} km_init_ctx;

static size_t km_align_up(size_t value, size_t alignment) {
  size_t remainder;
  if (alignment == 0) {
    return value;
  }
  remainder = value % alignment;
  if (remainder == 0) {
    return value;
  }
  return value + (alignment - remainder);
}

static int km_parse_point_line(const char* line, km_point* out_point) {
  char* x_end;
  char* y_end;
  double x;
  double y;
  const char* cursor;

  if (line == NULL || out_point == NULL) {
    return 0;
  }

  cursor = line;
  while (*cursor == ' ' || *cursor == '\t') {
    cursor++;
  }
  if (*cursor == '\0' || *cursor == '\n' || *cursor == '\r') {
    return 0;
  }

  errno = 0;
  x = strtod(cursor, &x_end);
  if (x_end == cursor || errno != 0) {
    return 0;
  }

  while (*x_end == ' ' || *x_end == '\t') {
    x_end++;
  }
  if (*x_end != ',') {
    return 0;
  }
  x_end++;
  while (*x_end == ' ' || *x_end == '\t') {
    x_end++;
  }

  errno = 0;
  y = strtod(x_end, &y_end);
  if (y_end == x_end || errno != 0) {
    return 0;
  }
  while (*y_end == ' ' || *y_end == '\t' || *y_end == '\n' || *y_end == '\r') {
    y_end++;
  }
  if (*y_end != '\0') {
    return 0;
  }

  out_point->x = x;
  out_point->y = y;
  return 1;
}

km_status km_load_points_csv(const char* path, km_point** out_points, size_t* out_count) {
  FILE* fp;
  char* line = NULL;
  size_t line_cap = 0;
  ssize_t line_len;
  km_point* points = NULL;
  size_t count = 0;
  size_t capacity = 0;
  int seen_data = 0;

  if (path == NULL || out_points == NULL || out_count == NULL) {
    return KM_ERR_INVALID_ARGUMENT;
  }

  fp = fopen(path, "r");
  if (fp == NULL) {
    return KM_ERR_IO;
  }

  while ((line_len = getline(&line, &line_cap, fp)) != -1) {
    km_point point;
    int parsed;
    (void)line_len;

    parsed = km_parse_point_line(line, &point);
    if (!parsed) {
      if (!seen_data) {
        continue;
      }
      free(points);
      free(line);
      fclose(fp);
      return KM_ERR_PARSE;
    }

    seen_data = 1;
    if (count == capacity) {
      km_point* next_points;
      size_t next_capacity = capacity == 0 ? 256u : capacity * 2u;
      next_points = (km_point*)realloc(points, next_capacity * sizeof(*next_points));
      if (next_points == NULL) {
        free(points);
        free(line);
        fclose(fp);
        return KM_ERR_ALLOC;
      }
      points = next_points;
      capacity = next_capacity;
    }
    points[count++] = point;
  }

  free(line);
  fclose(fp);

  if (count == 0) {
    free(points);
    return KM_ERR_PARSE;
  }

  *out_points = points;
  *out_count = count;
  return KM_OK;
}

void km_free_points(km_point* points) {
  free(points);
}

static inline double km_dist2(km_point a, km_point b) {
  double dx = a.x - b.x;
  double dy = a.y - b.y;
  return dx * dx + dy * dy;
}

static uint32_t km_lcg_next(uint32_t* state) {
  *state = *state * 1664525u + 1013904223u;
  return *state;
}

static km_status km_init_centers(
    const km_point* points,
    size_t n_points,
    int k,
    km_point* centers,
    uint32_t seed) {
  size_t* indices;
  size_t i;
  uint32_t rng_state = seed;

  if (points == NULL || centers == NULL || n_points == 0 || k <= 0 || (size_t)k > n_points) {
    return KM_ERR_INVALID_ARGUMENT;
  }

  indices = (size_t*)malloc(n_points * sizeof(*indices));
  if (indices == NULL) {
    return KM_ERR_ALLOC;
  }
  for (i = 0; i < n_points; ++i) {
    indices[i] = i;
  }

  for (i = 0; i < (size_t)k; ++i) {
    size_t span = n_points - i;
    size_t j = i + (size_t)(km_lcg_next(&rng_state) % (uint32_t)span);
    size_t tmp = indices[i];
    indices[i] = indices[j];
    indices[j] = tmp;
  }

  for (i = 0; i < (size_t)k; ++i) {
    centers[i] = points[indices[i]];
  }

  free(indices);
  return KM_OK;
}

static unsigned int km_iteration_delay_us(void) {
  const char* env = getenv("CM_KMEANS_ITER_DELAY_US");
  char* end;
  unsigned long value;

  if (env == NULL || env[0] == '\0') {
    return 0;
  }
  errno = 0;
  value = strtoul(env, &end, 10);
  if (errno != 0 || end == env || *end != '\0' || value > UINT_MAX) {
    return 0;
  }
  return (unsigned int)value;
}

static void km_sleep_us(unsigned int delay_us) {
  struct timespec ts;
  if (delay_us == 0u) {
    return;
  }
  ts.tv_sec = (time_t)(delay_us / 1000000u);
  ts.tv_nsec = (long)(delay_us % 1000000u) * 1000L;
  while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
  }
}

static km_status km_run_core(
    const km_point* points,
    size_t n_points,
    int k,
    int max_iter,
    km_point* centers,
    int* iteration_ptr,
    km_commit_fn commit_fn,
    void* commit_ctx,
    int resumed_from_iter,
    km_result* out_result) {
  int* labels = NULL;
  double* sum_x = NULL;
  double* sum_y = NULL;
  size_t* count = NULL;
  int iter;
  size_t i;
  int c;
  double inertia = 0.0;
  unsigned int delay_us = km_iteration_delay_us();
  km_point* centers_out = NULL;

  if (points == NULL || centers == NULL || iteration_ptr == NULL || out_result == NULL ||
      n_points == 0 || k <= 0 || max_iter < 0 || (size_t)k > n_points) {
    return KM_ERR_INVALID_ARGUMENT;
  }
  if (*iteration_ptr < 0) {
    return KM_ERR_INVALID_ARGUMENT;
  }

  labels = (int*)calloc(n_points, sizeof(*labels));
  sum_x = (double*)calloc((size_t)k, sizeof(*sum_x));
  sum_y = (double*)calloc((size_t)k, sizeof(*sum_y));
  count = (size_t*)calloc((size_t)k, sizeof(*count));
  if (labels == NULL || sum_x == NULL || sum_y == NULL || count == NULL) {
    free(labels);
    free(sum_x);
    free(sum_y);
    free(count);
    return KM_ERR_ALLOC;
  }

  iter = *iteration_ptr;
  if (iter > max_iter) {
    iter = max_iter;
    *iteration_ptr = iter;
  }

  while (iter < max_iter) {
    int moved = 0;

    for (i = 0; i < n_points; ++i) {
      int best_k = 0;
      double best_dist = km_dist2(points[i], centers[0]);
      for (c = 1; c < k; ++c) {
        double d = km_dist2(points[i], centers[c]);
        if (d < best_dist) {
          best_dist = d;
          best_k = c;
        }
      }
      labels[i] = best_k;
    }

    memset(sum_x, 0, (size_t)k * sizeof(*sum_x));
    memset(sum_y, 0, (size_t)k * sizeof(*sum_y));
    memset(count, 0, (size_t)k * sizeof(*count));

    for (i = 0; i < n_points; ++i) {
      int cluster = labels[i];
      sum_x[cluster] += points[i].x;
      sum_y[cluster] += points[i].y;
      count[cluster] += 1u;
    }

    for (c = 0; c < k; ++c) {
      if (count[c] == 0u) {
        continue;
      }

      {
        double nx = sum_x[c] / (double)count[c];
        double ny = sum_y[c] / (double)count[c];
        if (nx != centers[c].x || ny != centers[c].y) {
          moved = 1;
        }
        centers[c].x = nx;
        centers[c].y = ny;
      }
    }

    iter += 1;
    *iteration_ptr = iter;

    if (commit_fn != NULL) {
      km_status commit_status = commit_fn(commit_ctx);
      if (commit_status != KM_OK) {
        free(labels);
        free(sum_x);
        free(sum_y);
        free(count);
        return commit_status;
      }
    }

    if (delay_us > 0u) {
      km_sleep_us(delay_us);
    }

    if (!moved) {
      break;
    }
  }

  inertia = 0.0;
  for (i = 0; i < n_points; ++i) {
    int best_k = 0;
    double best_dist = km_dist2(points[i], centers[0]);
    for (c = 1; c < k; ++c) {
      double d = km_dist2(points[i], centers[c]);
      if (d < best_dist) {
        best_dist = d;
        best_k = c;
      }
    }
    labels[i] = best_k;
    inertia += best_dist;
  }

  centers_out = (km_point*)malloc((size_t)k * sizeof(*centers_out));
  if (centers_out == NULL) {
    free(labels);
    free(sum_x);
    free(sum_y);
    free(count);
    return KM_ERR_ALLOC;
  }
  memcpy(centers_out, centers, (size_t)k * sizeof(*centers_out));

  out_result->labels = labels;
  out_result->centers = centers_out;
  out_result->inertia = inertia;
  out_result->n_iter = iter;
  out_result->resumed_from_iter = resumed_from_iter;

  free(sum_x);
  free(sum_y);
  free(count);
  return KM_OK;
}

void km_result_free(km_result* result) {
  if (result == NULL) {
    return;
  }
  free(result->labels);
  free(result->centers);
  result->labels = NULL;
  result->centers = NULL;
  result->inertia = 0.0;
  result->n_iter = 0;
  result->resumed_from_iter = 0;
}

static cm_status km_init_centers_for_cm(void* ptr, size_t size, void* ctx) {
  km_init_ctx* init_ctx = (km_init_ctx*)ctx;
  km_status status;

  if (ptr == NULL || init_ctx == NULL || size != (size_t)init_ctx->k * sizeof(km_point)) {
    return CM_ERR_INVALID_ARGUMENT;
  }

  status = km_init_centers(
      init_ctx->points, init_ctx->n_points, init_ctx->k, (km_point*)ptr, KM_INIT_SEED);
  return status == KM_OK ? CM_OK : CM_ERR_IO;
}

static cm_status km_init_iteration_zero(void* ptr, size_t size, void* ctx) {
  (void)ctx;
  if (ptr == NULL || size != sizeof(int)) {
    return CM_ERR_INVALID_ARGUMENT;
  }
  *((int*)ptr) = 0;
  return CM_OK;
}

static km_status km_commit_with_cm(void* ctx) {
  cm_status status;

  (void)ctx;
  status = cm_commit();
  return cm_status_is_error(status) ? KM_ERR_CM : KM_OK;
}

km_status kmeans(
    const km_point* points,
    size_t n_points,
    int k,
    int max_iter,
    km_result* out_result) {
  km_point* centers;
  int iter = 0;
  km_status status;

  if (out_result == NULL) {
    return KM_ERR_INVALID_ARGUMENT;
  }
  memset(out_result, 0, sizeof(*out_result));

  if (points == NULL || n_points == 0 || k <= 0 || max_iter < 0 || (size_t)k > n_points) {
    return KM_ERR_INVALID_ARGUMENT;
  }

  centers = (km_point*)malloc((size_t)k * sizeof(*centers));
  if (centers == NULL) {
    return KM_ERR_ALLOC;
  }

  status = km_init_centers(points, n_points, k, centers, KM_INIT_SEED);
  if (status != KM_OK) {
    free(centers);
    return status;
  }

  status =
      km_run_core(points, n_points, k, max_iter, centers, &iter, NULL, NULL, 0, out_result);
  free(centers);
  return status;
}

km_status cm_kmeans(
    const char* shm_name,
    const km_point* points,
    size_t n_points,
    int k,
    int max_iter,
    km_result* out_result) {
  cm_open_opts opts;
  cm_status cm_status_code;
  km_status status;
  km_init_ctx init_ctx;
  km_point* centers = NULL;
  int* iteration_ptr = NULL;
  size_t needed_bytes;
  long page_size_long;
  size_t page_size;
  size_t logical_size;
  const char* effective_shm_name = shm_name != NULL ? shm_name : KM_DEFAULT_SHM_NAME;
  int resumed_from_iter;

  if (out_result == NULL) {
    return KM_ERR_INVALID_ARGUMENT;
  }
  memset(out_result, 0, sizeof(*out_result));

  if (points == NULL || n_points == 0 || k <= 0 || max_iter < 0 || (size_t)k > n_points) {
    return KM_ERR_INVALID_ARGUMENT;
  }

  page_size_long = sysconf(_SC_PAGESIZE);
  page_size = page_size_long > 0 ? (size_t)page_size_long : 4096u;
  needed_bytes = km_align_up(sizeof(int), 8u) + km_align_up((size_t)k * sizeof(km_point), 8u);
  logical_size = km_align_up(needed_bytes + page_size, page_size);
  if (logical_size < 2u * page_size) {
    logical_size = 2u * page_size;
  }

  opts.logical_size = logical_size;
  opts.flags = CM_OPEN_F_COMMIT_INTERVAL | CM_OPEN_F_COMMIT_MEMORY_PRESSURE;
  opts.commit_interval = 10;
  opts.commit_memory_threshold_percent = 90;
  cm_status_code = cm_open(effective_shm_name, &opts);
  if (cm_status_code != CM_OK) {
    return KM_ERR_CM;
  }

  init_ctx.points = points;
  init_ctx.n_points = n_points;
  init_ctx.k = k;

  cm_status_code = cm_get_oralloc(
      "kmeans_centers",
      (size_t)k * sizeof(km_point),
      km_init_centers_for_cm,
      &init_ctx,
      (void**)&centers);
  if (cm_status_code != CM_OK) {
    cm_close();
    return KM_ERR_CM;
  }

  cm_status_code = cm_get_oralloc(
      "kmeans_iteration", sizeof(int), km_init_iteration_zero, NULL, (void**)&iteration_ptr);
  if (cm_status_code != CM_OK) {
    cm_close();
    return KM_ERR_CM;
  }

  resumed_from_iter = *iteration_ptr;
  status = km_run_core(
      points,
      n_points,
      k,
      max_iter,
      centers,
      iteration_ptr,
      km_commit_with_cm,
      NULL,
      resumed_from_iter,
      out_result);

  cm_close();
  return status;
}

const char* km_status_string(km_status status) {
  switch (status) {
    case KM_OK:
      return "KM_OK";
    case KM_ERR_INVALID_ARGUMENT:
      return "KM_ERR_INVALID_ARGUMENT";
    case KM_ERR_IO:
      return "KM_ERR_IO";
    case KM_ERR_ALLOC:
      return "KM_ERR_ALLOC";
    case KM_ERR_PARSE:
      return "KM_ERR_PARSE";
    case KM_ERR_CM:
      return "KM_ERR_CM";
    default:
      return "KM_ERR_UNKNOWN";
  }
}
