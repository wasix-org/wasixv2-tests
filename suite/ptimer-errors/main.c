// timer_create, timer_settime, timer_gettime, timer_getoverrun and timer_delete errors, as
// Linux reports them: unknown clocks, clocks without timers, the alarm clocks without
// CAP_WAKE_ALARM, bad sigevents, bad timer ids and bad timespecs. Timer ids count up per
// process from 0.
//#ExpectedStdout: ok
#define _GNU_SOURCE
#include <errno.h>
#include <limits.h>
#include <signal.h>
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

#ifndef CLOCK_BOOTTIME_ALARM
#define CLOCK_BOOTTIME_ALARM 9
#endif

static int fails(clockid_t clock, struct sigevent *sev, int err)
{
	timer_t t;
	errno = 0;
	return timer_create(clock, sev, &t) == -1 && errno == err;
}

int main(void)
{
	timer_t a, b;
	CHECK(timer_create(CLOCK_MONOTONIC, NULL, &a) == 0);
	CHECK(timer_create(CLOCK_MONOTONIC, NULL, &b) == 0);
	CHECK((long)a == 0 && (long)b == 1);

	CHECK(fails(100, NULL, EINVAL));
	CHECK(fails(CLOCK_MONOTONIC_RAW, NULL, ENOTSUP));
	CHECK(fails(CLOCK_REALTIME_COARSE, NULL, ENOTSUP));
	CHECK(fails(CLOCK_MONOTONIC_COARSE, NULL, ENOTSUP));
	CHECK(fails(CLOCK_REALTIME_ALARM, NULL, EPERM));
	CHECK(fails(CLOCK_BOOTTIME_ALARM, NULL, EPERM));

	struct sigevent sev = {.sigev_notify = SIGEV_SIGNAL, .sigev_signo = 0};
	CHECK(fails(CLOCK_MONOTONIC, &sev, EINVAL));
	sev.sigev_signo = _NSIG;
	CHECK(fails(CLOCK_MONOTONIC, &sev, EINVAL));
	sev.sigev_signo = SIGRTMAX;
	timer_t t;
	CHECK(timer_create(CLOCK_MONOTONIC, &sev, &t) == 0);
	CHECK(timer_delete(t) == 0);
	sev.sigev_notify = 42;
	CHECK(fails(CLOCK_MONOTONIC, &sev, EINVAL));
	sev = (struct sigevent){.sigev_notify = SIGEV_NONE};
	CHECK(timer_create(CLOCK_MONOTONIC, &sev, &t) == 0);
	CHECK(timer_delete(t) == 0);

	/* The CPU clocks of this process and thread have timers. */
	CHECK(timer_create(CLOCK_PROCESS_CPUTIME_ID, NULL, &t) == 0);
	CHECK(timer_delete(t) == 0);
	CHECK(timer_create(CLOCK_THREAD_CPUTIME_ID, NULL, &t) == 0);
	CHECK(timer_delete(t) == 0);
	clockid_t cpu;
	CHECK(clock_getcpuclockid(0, &cpu) == 0);
	CHECK(timer_create(cpu, NULL, &t) == 0);
	CHECK(timer_delete(t) == 0);

	/* Bad timespecs and ids. */
	struct itimerspec spec = {.it_value = {0, 1000000000}};
	errno = 0;
	CHECK(timer_settime(a, 0, &spec, NULL) == -1 && errno == EINVAL);
	spec = (struct itimerspec){.it_value = {-1, 0}};
	errno = 0;
	CHECK(timer_settime(a, 0, &spec, NULL) == -1 && errno == EINVAL);
	spec = (struct itimerspec){.it_value = {1, 0}, .it_interval = {0, -1}};
	errno = 0;
	CHECK(timer_settime(a, 0, &spec, NULL) == -1 && errno == EINVAL);
	spec = (struct itimerspec){.it_value = {1, 0}};
	timer_t bad = (timer_t)(long)12345;
	errno = 0;
	CHECK(timer_settime(bad, 0, &spec, NULL) == -1 && errno == EINVAL);
	struct itimerspec cur;
	errno = 0;
	CHECK(timer_gettime(bad, &cur) == -1 && errno == EINVAL);
	errno = 0;
	CHECK(timer_getoverrun(bad) == -1 && errno == EINVAL);
	errno = 0;
	CHECK(timer_delete(bad) == -1 && errno == EINVAL);

	/* A large value is reported back as set. */
	spec = (struct itimerspec){.it_value = {INT_MAX, 0}, .it_interval = {3600, 5}};
	CHECK(timer_settime(a, 0, &spec, NULL) == 0);
	CHECK(timer_gettime(a, &cur) == 0);
	CHECK(cur.it_value.tv_sec > INT_MAX - 2 && cur.it_value.tv_sec <= INT_MAX);
	CHECK(cur.it_interval.tv_sec == 3600 && cur.it_interval.tv_nsec == 5);
	/* An interval without a value leaves the timer disarmed. */
	spec = (struct itimerspec){.it_interval = {1, 0}};
	CHECK(timer_settime(a, 0, &spec, NULL) == 0);
	CHECK(timer_gettime(a, &cur) == 0);
	CHECK(cur.it_value.tv_sec == 0 && cur.it_value.tv_nsec == 0 && cur.it_interval.tv_sec == 0);

	CHECK(timer_delete(a) == 0);
	CHECK(timer_delete(b) == 0);
	if (failures)
		return 1;
	puts("ok");
	return 0;
}
