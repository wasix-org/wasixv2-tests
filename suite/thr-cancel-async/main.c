// A compute-only thread accepts asynchronous cancellation at the existing
// preemption points; no syscall or pthread_testcancel is needed in its loop.
//#ExpectedStdout: canceled=1 cleanup=1
#include <pthread.h>
#include <stdio.h>
#include <time.h>

static int ready, cleanup;

static void on_cancel(void* arg) {
  (void)arg;
  cleanup = 1;
}

static void* run(void* arg) {
  (void)arg;
  pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
  pthread_cleanup_push(on_cancel, NULL);
  __atomic_store_n(&ready, 1, __ATOMIC_RELEASE);
  for (volatile unsigned long i = 0;; i++) {
  }
  pthread_cleanup_pop(0);
  return NULL;
}

int main(void) {
  pthread_t thread;
  if (pthread_create(&thread, NULL, run, NULL)) return 1;
  while (!__atomic_load_n(&ready, __ATOMIC_ACQUIRE)) {
  }
  struct timespec pause = {0, 50000000};
  nanosleep(&pause, NULL);
  if (pthread_cancel(thread)) return 1;
  void* result;
  if (pthread_join(thread, &result)) return 1;
  printf("canceled=%d cleanup=%d\n", result == PTHREAD_CANCELED, cleanup);
  return result != PTHREAD_CANCELED || cleanup != 1;
}
