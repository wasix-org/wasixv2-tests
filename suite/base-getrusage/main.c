// getrusage for the process, the thread and (absent) children, times, and clock: CPU time
// grows while the program computes.
//#ExpectedStdout: ok
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/times.h>
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

static long long now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static void spin(int ms)
{
	long long end = now() + ms * 1000000LL;
	volatile unsigned x = 0;
	while (now() < end)
		for (int i = 0; i < 1000; i++)
			x += i;
}

static long long cpu_us(const struct rusage *ru)
{
	return (ru->ru_utime.tv_sec + ru->ru_stime.tv_sec) * 1000000LL + ru->ru_utime.tv_usec +
	       ru->ru_stime.tv_usec;
}

int main(void)
{
	struct rusage before, after, thread, children;
	CHECK(getrusage(RUSAGE_SELF, &before) == 0);
	clock_t c0 = clock();
	struct tms t0;
	clock_t ticks0 = times(&t0);
	CHECK(ticks0 != (clock_t)-1);

	spin(50);

	CHECK(getrusage(RUSAGE_SELF, &after) == 0);
	CHECK(after.ru_utime.tv_usec >= 0 && after.ru_utime.tv_usec < 1000000);
	CHECK(after.ru_stime.tv_usec >= 0 && after.ru_stime.tv_usec < 1000000);
	CHECK(cpu_us(&after) - cpu_us(&before) >= 20000);
	CHECK(after.ru_maxrss > 0);

	CHECK(getrusage(RUSAGE_THREAD, &thread) == 0);
	CHECK(cpu_us(&thread) >= 20000);

	memset(&children, 0xff, sizeof children);
	CHECK(getrusage(RUSAGE_CHILDREN, &children) == 0);
	CHECK(cpu_us(&children) == 0 && children.ru_maxrss == 0);

	errno = 0;
	CHECK(getrusage(5, &after) == -1 && errno == EINVAL);

	clock_t c1 = clock();
	CHECK(c0 != (clock_t)-1 && c1 - c0 >= CLOCKS_PER_SEC / 50);

	struct tms t1;
	clock_t ticks1 = times(&t1);
	CHECK(ticks1 != (clock_t)-1 && ticks1 >= ticks0);
	CHECK(t1.tms_utime + t1.tms_stime >= t0.tms_utime + t0.tms_stime + 2);
	CHECK(t1.tms_cutime == 0 && t1.tms_cstime == 0);
	CHECK(times(NULL) != (clock_t)-1);
	CHECK(sysconf(_SC_CLK_TCK) == 100);

	if (failures)
		return 1;
	puts("ok");
	return 0;
}
