/*
 * K-PHD Stress Test: CPU Hog
 *
 * Spawns multiple threads that consume 100% CPU in tight loops,
 * starving other processes of scheduling time and triggering
 * K-PHD's latency detection.
 *
 * Usage: ./cpu_hog [num_threads] [duration_seconds]
 *   Default: 8 threads for 10 seconds
 */

#define _GNU_SOURCE
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

static volatile int running = 1;

static void sig_handler(int sig) {
  (void)sig;
  running = 0;
}

/* Pure CPU burn — no syscalls, no yields */
static void *cpu_burn(void *arg) {
  int id = *(int *)arg;
  volatile double x = 1.0;

  printf("  [Thread %d] Burning CPU (PID %d, TID %ld)\n", id, getpid(),
         (long)gettid());

  while (running) {
    /* Tight arithmetic loop — keeps the CPU fully occupied */
    for (int i = 0; i < 1000000; i++)
      x = x * 1.0001 + 0.0001;
  }

  printf("  [Thread %d] Stopped (x=%.2f)\n", id, x);
  return NULL;
}

int main(int argc, char *argv[]) {
  int num_threads = 8;
  int duration = 10;

  if (argc > 1)
    num_threads = atoi(argv[1]);
  if (argc > 2)
    duration = atoi(argv[2]);

  if (num_threads < 1)
    num_threads = 1;
  if (num_threads > 256)
    num_threads = 256;

  signal(SIGINT, sig_handler);
  signal(SIGTERM, sig_handler);

  printf("\n\033[1;31m╔═══════════════════════════════════════╗\033[0m\n");
  printf("\033[1;31m║  K-PHD Stress Test: CPU Hog           ║\033[0m\n");
  printf("\033[1;31m╚═══════════════════════════════════════╝\033[0m\n\n");
  printf("Threads: %d | Duration: %d seconds\n\n", num_threads, duration);

  pthread_t *threads = malloc(sizeof(pthread_t) * num_threads);
  int *ids = malloc(sizeof(int) * num_threads);

  /* Spawn CPU-burning threads */
  for (int i = 0; i < num_threads; i++) {
    ids[i] = i;
    pthread_create(&threads[i], NULL, cpu_burn, &ids[i]);
  }

  /* Run for specified duration */
  sleep(duration);
  running = 0;

  printf("\nStopping threads...\n");
  for (int i = 0; i < num_threads; i++)
    pthread_join(threads[i], NULL);

  free(threads);
  free(ids);

  printf("\n\033[1;32mCPU hog test complete.\033[0m\n");
  printf("Check K-PHD output: cat /proc/kphd_stats\n\n");
  return 0;
}
