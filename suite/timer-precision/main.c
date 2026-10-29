// Linux kernel/time/hrtimer.c hrtimer_nanosleep: nanosleep uses nanosecond
// deadlines and PR_SET_TIMERSLACK, without adding a millisecond scheduler tick.
//#ExpectedStdout: ok
#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <time.h>

static long long now(void) {
  struct timespec ts;
  assert(clock_gettime(CLOCK_MONOTONIC, &ts) == 0);
  return ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static int compare(const void* a, const void* b) {
  long long x = *(const long long*)a, y = *(const long long*)b;
  return (x > y) - (x < y);
}

int main(void) {
  assert(prctl(PR_SET_TIMERSLACK, 1UL) == 0);
  long long overshoot[64];
  for (int i = 0; i < 64; i++) {
    struct timespec req = {0, 1000000};
    long long start = now();
    assert(nanosleep(&req, NULL) == 0);
    overshoot[i] = now() - start - 1000000;
    assert(overshoot[i] >= 0);
  }
  qsort(overshoot, 64, sizeof overshoot[0], compare);
  if (overshoot[32] >= 450000) {
    printf("median nanosleep overshoot: %lld ns (limit 450000)\n",
           overshoot[32]);
    return 1;
  }
  puts("ok");
  return 0;
}
