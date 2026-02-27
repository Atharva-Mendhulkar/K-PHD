/*
 * K-PHD Stress Test: IO Stall Simulator
 *
 * Creates processes that alternate between:
 *   1. Heavy synchronous I/O (fsync storms)
 *   2. Lock contention (futex spinning)
 *   3. Memory pressure (mmap thrashing)
 *
 * This causes scheduling delays visible to K-PHD because
 * processes get blocked in I/O wait states and experience
 * runqueue starvation when they wake up.
 *
 * Usage: ./io_stall [num_workers] [duration_seconds]
 *   Default: 4 workers for 10 seconds
 */

#define _GNU_SOURCE
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

static volatile int running = 1;

static void sig_handler(int sig) {
  (void)sig;
  running = 0;
}

/* Write + fsync storm — blocks on disk I/O */
static void *io_storm(void *arg) {
  int id = *(int *)arg;
  char filename[64];
  char buf[4096];
  int count = 0;

  snprintf(filename, sizeof(filename), "/tmp/kphd_io_test_%d", id);
  memset(buf, 'X', sizeof(buf));

  printf("  [IO Worker %d] Starting fsync storm (PID %d)\n", id, getpid());

  int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) {
    perror("open");
    return NULL;
  }

  while (running) {
    /* Write 4KB then force sync to disk */
    write(fd, buf, sizeof(buf));
    fsync(fd);
    count++;

    /* Seek back to beginning periodically to avoid filling disk */
    if (count % 100 == 0)
      lseek(fd, 0, SEEK_SET);
  }

  close(fd);
  unlink(filename);
  printf("  [IO Worker %d] Stopped (%d fsyncs)\n", id, count);
  return NULL;
}

/* Lock contention — threads fight over the same mutex */
static pthread_mutex_t contention_lock = PTHREAD_MUTEX_INITIALIZER;
static volatile long shared_counter = 0;

static void *lock_contention(void *arg) {
  int id = *(int *)arg;
  int count = 0;

  printf("  [Lock Worker %d] Starting lock contention (PID %d)\n", id,
         getpid());

  while (running) {
    pthread_mutex_lock(&contention_lock);
    /* Do some work under the lock to increase contention */
    for (volatile int i = 0; i < 1000; i++)
      shared_counter++;
    pthread_mutex_unlock(&contention_lock);
    count++;
  }

  printf("  [Lock Worker %d] Stopped (%d acquisitions)\n", id, count);
  return NULL;
}

int main(int argc, char *argv[]) {
  int num_workers = 4;
  int duration = 10;

  if (argc > 1)
    num_workers = atoi(argv[1]);
  if (argc > 2)
    duration = atoi(argv[2]);
  if (num_workers < 1)
    num_workers = 1;
  if (num_workers > 64)
    num_workers = 64;

  signal(SIGINT, sig_handler);
  signal(SIGTERM, sig_handler);

  printf("\n\033[1;33m╔═══════════════════════════════════════╗\033[0m\n");
  printf("\033[1;33m║  K-PHD Stress Test: IO Stall + Lock   ║\033[0m\n");
  printf("\033[1;33m╚═══════════════════════════════════════╝\033[0m\n\n");
  printf("Workers: %d (IO) + %d (Lock) | Duration: %d seconds\n\n", num_workers,
         num_workers, duration);

  int total = num_workers * 2;
  pthread_t *threads = malloc(sizeof(pthread_t) * total);
  int *ids = malloc(sizeof(int) * total);

  /* Spawn IO storm workers */
  for (int i = 0; i < num_workers; i++) {
    ids[i] = i;
    pthread_create(&threads[i], NULL, io_storm, &ids[i]);
  }

  /* Spawn lock contention workers */
  for (int i = 0; i < num_workers; i++) {
    ids[num_workers + i] = i;
    pthread_create(&threads[num_workers + i], NULL, lock_contention,
                   &ids[num_workers + i]);
  }

  sleep(duration);
  running = 0;

  printf("\nStopping workers...\n");
  for (int i = 0; i < total; i++)
    pthread_join(threads[i], NULL);

  free(threads);
  free(ids);

  printf("\n\033[1;32mIO stall test complete.\033[0m\n");
  printf("Check K-PHD output: cat /proc/kphd_stats\n\n");
  return 0;
}
