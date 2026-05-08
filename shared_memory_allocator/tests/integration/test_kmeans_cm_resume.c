#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "../../apps/kmeans/kmeans.h"
#include "test_helpers.h"

#ifndef CM_KMEANS_TEST_DATA_PATH
#define CM_KMEANS_TEST_DATA_PATH "apps/kmeans/data/points.csv"
#endif

static void sleep_us(unsigned int delay_us) {
  struct timespec ts;
  if (delay_us == 0u) {
    return;
  }
  ts.tv_sec = (time_t)(delay_us / 1000000u);
  ts.tv_nsec = (long)(delay_us % 1000000u) * 1000L;
  while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
  }
}

static double abs_diff(double a, double b) {
  double diff = a - b;
  return diff >= 0.0 ? diff : -diff;
}

static int results_equal(const km_result* a, const km_result* b, size_t n_points, int k) {
  size_t i;
  const double tol = 1e-9;

  if (a->n_iter != b->n_iter) {
    fprintf(stderr, "n_iter mismatch: baseline=%d resumed=%d\n", a->n_iter, b->n_iter);
    return 0;
  }
  if (abs_diff(a->inertia, b->inertia) > tol) {
    fprintf(stderr, "inertia mismatch: baseline=%.12f resumed=%.12f\n", a->inertia, b->inertia);
    return 0;
  }

  for (i = 0; i < (size_t)k; ++i) {
    if (abs_diff(a->centers[i].x, b->centers[i].x) > tol ||
        abs_diff(a->centers[i].y, b->centers[i].y) > tol) {
      fprintf(
          stderr,
          "center[%zu] mismatch: baseline=(%.12f, %.12f) resumed=(%.12f, %.12f)\n",
          i,
          a->centers[i].x,
          a->centers[i].y,
          b->centers[i].x,
          b->centers[i].y);
      return 0;
    }
  }

  for (i = 0; i < n_points; ++i) {
    if (a->labels[i] != b->labels[i]) {
      fprintf(stderr, "label[%zu] mismatch: baseline=%d resumed=%d\n", i, a->labels[i], b->labels[i]);
      return 0;
    }
  }

  return 1;
}

int main(void) {
  const char* shm_name = "/cm_integration_kmeans_resume";
  const int k = 12;
  const int max_iter = 200;
  km_point* points = NULL;
  size_t n_points = 0;
  km_result baseline;
  km_result resumed;
  km_status status;
  pid_t pid;
  int wait_status;

  memset(&baseline, 0, sizeof(baseline));
  memset(&resumed, 0, sizeof(resumed));

  th_cleanup_shm(shm_name);

  status = km_load_points_csv(CM_KMEANS_TEST_DATA_PATH, &points, &n_points);
  if (status != KM_OK) {
    fprintf(stderr, "failed to load dataset %s: %s\n", CM_KMEANS_TEST_DATA_PATH, km_status_string(status));
    return 1;
  }
  if ((size_t)k > n_points) {
    fprintf(stderr, "dataset too small for k=%d (points=%zu)\n", k, n_points);
    km_free_points(points);
    return 1;
  }

  status = kmeans(points, n_points, k, max_iter, &baseline);
  if (status != KM_OK) {
    fprintf(stderr, "baseline kmeans failed: %s\n", km_status_string(status));
    km_free_points(points);
    return 1;
  }

  pid = fork();
  if (pid < 0) {
    perror("fork");
    km_result_free(&baseline);
    km_free_points(points);
    return 1;
  }
  if (pid == 0) {
    km_result child_result;
    memset(&child_result, 0, sizeof(child_result));
    (void)setenv("CM_KMEANS_ITER_DELAY_US", "50000", 1);
    (void)cm_kmeans(shm_name, points, n_points, k, max_iter, &child_result);
    km_result_free(&child_result);
    _exit(0);
  }

  sleep_us(650000u);
  if (kill(pid, SIGKILL) != 0) {
    perror("kill");
    km_result_free(&baseline);
    km_free_points(points);
    th_cleanup_shm(shm_name);
    return 1;
  }
  if (waitpid(pid, &wait_status, 0) < 0) {
    perror("waitpid");
    km_result_free(&baseline);
    km_free_points(points);
    th_cleanup_shm(shm_name);
    return 1;
  }
  if (!WIFSIGNALED(wait_status) || WTERMSIG(wait_status) != SIGKILL) {
    fprintf(stderr, "expected child to terminate via SIGKILL, status=%d\n", wait_status);
    km_result_free(&baseline);
    km_free_points(points);
    th_cleanup_shm(shm_name);
    return 1;
  }

  (void)unsetenv("CM_KMEANS_ITER_DELAY_US");
  status = cm_kmeans(shm_name, points, n_points, k, max_iter, &resumed);
  if (status != KM_OK) {
    fprintf(stderr, "resume cm_kmeans failed: %s\n", km_status_string(status));
    km_result_free(&baseline);
    km_free_points(points);
    th_cleanup_shm(shm_name);
    return 1;
  }

  if (resumed.resumed_from_iter <= 0) {
    fprintf(stderr, "expected resumed run to start from persisted iteration > 0, got %d\n", resumed.resumed_from_iter);
    km_result_free(&resumed);
    km_result_free(&baseline);
    km_free_points(points);
    th_cleanup_shm(shm_name);
    return 1;
  }

  if (!results_equal(&baseline, &resumed, n_points, k)) {
    km_result_free(&resumed);
    km_result_free(&baseline);
    km_free_points(points);
    th_cleanup_shm(shm_name);
    return 1;
  }

  km_result_free(&resumed);
  km_result_free(&baseline);
  km_free_points(points);
  th_cleanup_shm(shm_name);
  return 0;
}
