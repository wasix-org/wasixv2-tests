// nanosleep, clock_nanosleep (relative and TIMER_ABSTIME, on several clocks), usleep and
// sleep: they sleep at least as long as asked, and reject bad clocks and timespecs.
//#ExpectedStdout: ok
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <string.h>
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

#define MS 1000000LL

static long long now(clockid_t clock)
{
	struct timespec ts;
	clock_gettime(clock, &ts);
	return ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static struct timespec at(long long ns)
{
	return (struct timespec){ns / 1000000000LL, ns % 1000000000LL};
}

int main(void)
{
	long long start = now(CLOCK_MONOTONIC);
	struct timespec req = at(30 * MS), rem = {-1, -1};
	CHECK(nanosleep(&req, &rem) == 0);
	CHECK(now(CLOCK_MONOTONIC) - start >= 30 * MS);

	req = (struct timespec){0, 0};
	CHECK(nanosleep(&req, NULL) == 0);

	req = (struct timespec){0, 1000000000};
	errno = 0;
	CHECK(nanosleep(&req, NULL) == -1 && errno == EINVAL);
	req = (struct timespec){-1, 0};
	errno = 0;
	CHECK(nanosleep(&req, NULL) == -1 && errno == EINVAL);
	req = (struct timespec){0, -1};
	errno = 0;
	CHECK(nanosleep(&req, NULL) == -1 && errno == EINVAL);

	/* Relative sleeps on the monotonic, boot and wall clocks. */
	clockid_t relative[] = {CLOCK_MONOTONIC, CLOCK_BOOTTIME, CLOCK_REALTIME};
	for (int i = 0; i < 3; i++) {
		start = now(CLOCK_MONOTONIC);
		req = at(20 * MS);
		CHECK(clock_nanosleep(relative[i], 0, &req, NULL) == 0);
		CHECK(now(CLOCK_MONOTONIC) - start >= 20 * MS);
	}

	/* Absolute deadlines are reached, never undershot. */
	long long deadline = now(CLOCK_MONOTONIC) + 30 * MS;
	req = at(deadline);
	CHECK(clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &req, NULL) == 0);
	CHECK(now(CLOCK_MONOTONIC) >= deadline);

	deadline = now(CLOCK_REALTIME) + 30 * MS;
	req = at(deadline);
	CHECK(clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &req, NULL) == 0);
	CHECK(now(CLOCK_REALTIME) >= deadline);

	/* A deadline in the past returns at once. */
	start = now(CLOCK_MONOTONIC);
	req = at(start - 1000 * MS);
	CHECK(clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &req, NULL) == 0);
	CHECK(now(CLOCK_MONOTONIC) - start < 500 * MS);

	/* clock_nanosleep returns the error instead of setting errno. */
	req = at(MS);
	CHECK(clock_nanosleep(CLOCK_THREAD_CPUTIME_ID, 0, &req, NULL) == EINVAL);
	CHECK(clock_nanosleep(100, 0, &req, NULL) == EINVAL);
	req = (struct timespec){0, 1000000000};
	CHECK(clock_nanosleep(CLOCK_MONOTONIC, 0, &req, NULL) == EINVAL);

	start = now(CLOCK_MONOTONIC);
	CHECK(usleep(15000) == 0);
	CHECK(now(CLOCK_MONOTONIC) - start >= 15 * MS);
	CHECK(sleep(0) == 0);

	if (failures)
		return 1;
	puts("ok");
	return 0;
}
