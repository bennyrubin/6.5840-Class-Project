#ifndef CM_APP_KMEANS_H
#define CM_APP_KMEANS_H

#include <stddef.h>

typedef struct km_point {
  double x;
  double y;
} km_point;

typedef struct km_result {
  int* labels;
  km_point* centers;
  double inertia;
  int n_iter;
  int resumed_from_iter;
} km_result;

typedef enum km_status {
  KM_OK = 0,
  KM_ERR_INVALID_ARGUMENT = 1,
  KM_ERR_IO = 2,
  KM_ERR_ALLOC = 3,
  KM_ERR_PARSE = 4,
  KM_ERR_CM = 5,
} km_status;

km_status km_load_points_csv(const char* path, km_point** out_points, size_t* out_count);
void km_free_points(km_point* points);

km_status kmeans(
    const km_point* points,
    size_t n_points,
    int k,
    int max_iter,
    km_result* out_result);

km_status cm_kmeans(
    const char* shm_name,
    const km_point* points,
    size_t n_points,
    int k,
    int max_iter,
    km_result* out_result);

void km_result_free(km_result* result);
const char* km_status_string(km_status status);

#endif
