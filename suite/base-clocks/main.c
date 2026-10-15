// clock_gettime and clock_getres on every clock a program reads, the CPU-time clocks
// (process, thread, and per-pid ids from clock_getcpuclockid), and their error cases.
//#ExpectedStdout: ok
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static int failures;
#define CHECK(cond)                                                                       \
	do {                                                                              \
		if (!(cond)) {                                                            \
			printf("FAIL line %d: %s (errno %d: %s)\n", __LINE__, #cond, errno, \
			       strerror(errno));                                          \
			failures++;                                                       \
		}                                                                         \
	} while (0)

static const struct {
	clockid_t id;
	const char *name;
} clocks[] = {
	{CLOCK_REALTIME, "REALTIME"},
	{CLOCK_MONOTONIC, "MONOTONIC"},
	{CLOCK_PROCESS_CPUTIME_ID, "PROCESS_CPUTIME_ID"},
	{CLOCK_THREAD_CPUTIME_ID, "THREAD_CPUTIME_ID"},
	{CLOCK_MONOTONIC_RAW, "MONOTONIC_RAW"},
	{CLOCK_REALTIME_COARSE, "REALTIME_COARSE"},
	{CLOCK_MONOTONIC_COARSE, "MONOTONIC_COARSE"},
	{CLOCK_BOOTTIME, "BOOTTIME"},
	{CLOCK_TAI, "TAI"},
};

static long long ns(struct timespec ts)
{
	return ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static long long now(clockid_t clock)
{
	struct timespec ts;
	clock_gettime(clock, &ts);
	return ns(ts);
}

/* Spins until the monotonic clock advanced by `ms`, burning CPU time. */
static void spin(int ms)
{
	long long end = now(CLOCK_MONOTONIC) + ms * 1000000LL;
	volatile unsigned x = 0;
	while (now(CLOCK_MONOTONIC) < end)
		for (int i = 0; i < 1000; i++)
			x += i;
}

int main(void)
{
	for (size_t i = 0; i < sizeof clocks / sizeof clocks[0]; i++) {
		struct timespec ts = {-1, -1}, res = {-1, -1};
		errno = 0;
		int r = clock_gettime(clocks[i].id, &ts);
		if (r != 0 || ts.tv_nsec < 0 || ts.tv_nsec >= 1000000000 || ts.tv_sec < 0)
			printf("FAIL clock_gettime(%s) = %d errno %d\n", clocks[i].name, r, errno),
				failures++;
		r = clock_getres(clocks[i].id, &res);
		if (r != 0 || res.tv_sec != 0 || res.tv_nsec <= 0 || res.tv_nsec > 10000000)
			printf("FAIL clock_getres(%s) = %d {%lld, %ld} errno %d\n", clocks[i].name,
			       r, (long long)res.tv_sec, res.tv_nsec, errno),
				failures++;
		if (clock_getres(clocks[i].id, NULL) != 0)
			printf("FAIL clock_getres(%s, NULL)\n", clocks[i].name), failures++;
	}

	struct timespec ts;
	errno = 0;
	CHECK(clock_gettime(100, &ts) == -1 && errno == EINVAL);
	errno = 0;
	CHECK(clock_getres(100, &ts) == -1 && errno == EINVAL);

	/* Wall time is after 2020 and agrees with gettimeofday and time. */
	long long real = now(CLOCK_REALTIME);
	CHECK(real > 1577836800LL * 1000000000LL);
	struct timeval tv;
	CHECK(gettimeofday(&tv, NULL) == 0);
	long long tv_ns = tv.tv_sec * 1000000000LL + tv.tv_usec * 1000LL;
	/* gettimeofday truncates to microseconds. */
	CHECK(tv_ns - real > -1000 && tv_ns - real < 1000000000LL);
	time_t t = time(NULL);
	CHECK(t >= real / 1000000000LL && t - real / 1000000000LL <= 1);
	CHECK(now(CLOCK_TAI) >= real);

	/* Monotonic clocks never go backwards. */
	long long last = now(CLOCK_MONOTONIC);
	for (int i = 0; i < 10000; i++) {
		long long cur = now(CLOCK_MONOTONIC);
		if (cur < last) {
			printf("FAIL monotonic went back by %lld ns\n", last - cur);
			failures++;
			break;
		}
		last = cur;
	}

	/* CPU time advances while the program computes. */
	long long proc0 = now(CLOCK_PROCESS_CPUTIME_ID);
	long long thr0 = now(CLOCK_THREAD_CPUTIME_ID);
	spin(30);
	long long proc1 = now(CLOCK_PROCESS_CPUTIME_ID);
	long long thr1 = now(CLOCK_THREAD_CPUTIME_ID);
	CHECK(proc1 - proc0 >= 10000000);
	CHECK(thr1 - thr0 >= 10000000);

	/* Per-process and per-thread CPU clock ids. */
	clockid_t cid;
	CHECK(clock_getcpuclockid(0, &cid) == 0);
	CHECK(clock_gettime(cid, &ts) == 0);
	CHECK(clock_getcpuclockid(getpid(), &cid) == 0);
	CHECK(clock_gettime(cid, &ts) == 0 && ns(ts) >= proc1);
	CHECK(clock_getcpuclockid(99999, &cid) == ESRCH);
	CHECK(pthread_getcpuclockid(pthread_self(), &cid) == 0);
	CHECK(clock_gettime(cid, &ts) == 0 && ns(ts) >= thr1);

	if (failures)
		return 1;
	puts("ok");
	return 0;
}
