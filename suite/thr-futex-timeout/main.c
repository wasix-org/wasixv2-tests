// Raw futex: EAGAIN on a value mismatch, ETIMEDOUT after a relative timeout, EINVAL on a
// misaligned address, and FUTEX_WAKE from another thread ends a FUTEX_WAIT.
//#ExpectedStdout: mismatch=EAGAIN timeout=ETIMEDOUT elapsed_ok=1 misaligned=EINVAL woken=0 word=1
//#ExpectedExitCode: 0
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

/* <linux/futex.h> is not part of the sysroot; asm-generic values. */
#define FUTEX_WAIT_PRIVATE 128
#define FUTEX_WAKE_PRIVATE 129

static int word;

static long futex(int *addr, int op, int val, const struct timespec *ts)
{
	return syscall(SYS_futex, addr, op, val, ts, 0, 0);
}

static const char *err(long r)
{
	if (r == 0) return "0";
	switch (errno) {
	case EAGAIN: return "EAGAIN";
	case ETIMEDOUT: return "ETIMEDOUT";
	case EINVAL: return "EINVAL";
	default: return strerror(errno);
	}
}

static void *waker(void *arg)
{
	struct timespec d = {0, 50 * 1000 * 1000};
	nanosleep(&d, 0);
	__atomic_store_n(&word, 1, __ATOMIC_SEQ_CST);
	futex(&word, FUTEX_WAKE_PRIVATE, 1, 0);
	return 0;
}

int main(void)
{
	printf("mismatch=%s ", err(futex(&word, FUTEX_WAIT_PRIVATE, 5, 0)));
	struct timespec t0, t1, ts = {0, 100 * 1000 * 1000};
	clock_gettime(CLOCK_MONOTONIC, &t0);
	printf("timeout=%s ", err(futex(&word, FUTEX_WAIT_PRIVATE, 0, &ts)));
	clock_gettime(CLOCK_MONOTONIC, &t1);
	long ms = (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000;
	printf("elapsed_ok=%d ", ms >= 100);
	printf("misaligned=%s ", err(futex((int *)((char *)&word + 1), FUTEX_WAIT_PRIVATE, 0, 0)));
	pthread_t t;
	pthread_create(&t, 0, waker, 0);
	long r = 0;
	while (__atomic_load_n(&word, __ATOMIC_SEQ_CST) == 0) {
		r = futex(&word, FUTEX_WAIT_PRIVATE, 0, 0);
		if (r && errno != EAGAIN) break;
		r = 0;
	}
	printf("woken=%s word=%d\n", err(r), word);
	pthread_join(t, 0);
	return 0;
}
