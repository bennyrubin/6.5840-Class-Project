#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kmeans.h"

#ifndef CM_KMEANS_DEFAULT_DATA_PATH
#define CM_KMEANS_DEFAULT_DATA_PATH "data/points.csv"
#endif

typedef enum app_mode {
  APP_MODE_NORMAL = 0,
  APP_MODE_CM = 1,
} app_mode;

static void print_usage(const char* argv0) {
  fprintf(stderr, "Usage: %s [--data PATH] [--k N] [--max-iter N] [--mode normal|cm] [--shm-name NAME]\n", argv0);
}

int main(int argc, char** argv) {
  const char* data_path = CM_KMEANS_DEFAULT_DATA_PATH;
  const char* shm_name = "/cm_kmeans_app";
  int k = 4;
  int max_iter = 300;
  app_mode mode = APP_MODE_NORMAL;
  km_point* points = NULL;
  size_t n_points = 0;
  km_result result;
  km_status status;
  size_t i;
  int* counts;

  memset(&result, 0, sizeof(result));

  for (i = 1; i < (size_t)argc; ++i) {
    if (strcmp(argv[i], "--data") == 0 && i + 1 < (size_t)argc) {
      data_path = argv[++i];
    } else if (strcmp(argv[i], "--k") == 0 && i + 1 < (size_t)argc) {
      k = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--max-iter") == 0 && i + 1 < (size_t)argc) {
      max_iter = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--mode") == 0 && i + 1 < (size_t)argc) {
      const char* mode_arg = argv[++i];
      if (strcmp(mode_arg, "normal") == 0) {
        mode = APP_MODE_NORMAL;
      } else if (strcmp(mode_arg, "cm") == 0) {
        mode = APP_MODE_CM;
      } else {
        print_usage(argv[0]);
        return 1;
      }
    } else if (strcmp(argv[i], "--shm-name") == 0 && i + 1 < (size_t)argc) {
      shm_name = argv[++i];
    } else if (strcmp(argv[i], "--help") == 0) {
      print_usage(argv[0]);
      return 0;
    } else {
      print_usage(argv[0]);
      return 1;
    }
  }

  status = km_load_points_csv(data_path, &points, &n_points);
  if (status != KM_OK) {
    fprintf(stderr, "failed to load %s: %s\n", data_path, km_status_string(status));
    return 1;
  }

  if ((size_t)k > n_points || k <= 0) {
    fprintf(stderr, "invalid k=%d for %zu points\n", k, n_points);
    km_free_points(points);
    return 1;
  }

  if (mode == APP_MODE_CM) {
    status = cm_kmeans(shm_name, points, n_points, k, max_iter, &result);
  } else {
    status = kmeans(points, n_points, k, max_iter, &result);
  }
  if (status != KM_OK) {
    fprintf(stderr, "kmeans failed: %s\n", km_status_string(status));
    km_free_points(points);
    return 1;
  }

  printf("K-Means (C)\n");
  printf("Data file : %s\n", data_path);
  printf("Mode      : %s\n", mode == APP_MODE_CM ? "cm" : "normal");
  printf("Points    : %zu\n", n_points);
  printf("Clusters  : %d\n", k);
  printf("Iterations: %d\n", result.n_iter);
  if (mode == APP_MODE_CM) {
    printf("Resumed from iteration: %d\n", result.resumed_from_iter);
  }
  printf("Inertia   : %.10f\n", result.inertia);
  printf("Centers:\n");
  for (i = 0; i < (size_t)k; ++i) {
    printf("  [%zu] %.10f, %.10f\n", i, result.centers[i].x, result.centers[i].y);
  }

  counts = (int*)calloc((size_t)k, sizeof(*counts));
  if (counts != NULL) {
    for (i = 0; i < n_points; ++i) {
      int label = result.labels[i];
      if (label >= 0 && label < k) {
        counts[label] += 1;
      }
    }
    printf("Cluster counts:\n");
    for (i = 0; i < (size_t)k; ++i) {
      printf("  [%zu] %d\n", i, counts[i]);
    }
    free(counts);
  }

  km_result_free(&result);
  km_free_points(points);
  return 0;
}
