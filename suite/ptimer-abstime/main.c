// TIMER_ABSTIME: an absolute expiry already past fires at once, a future one on
// CLOCK_REALTIME or CLOCK_MONOTONIC fires when that clock reaches it, and gettime reports the
// time left. An absolute interval timer goes on with its interval. CPU-time clocks have timers
// too.
//#ExpectedStdout: ok
#define _GNU_SOURCE
#include <errno.h>
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

static long long ns(struct timespec ts)
{
	return ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static sigset_t set;

/* Waits up to `ms` for the timer signal; returns whether it came. */
static int took(int ms)
{
	struct timespec wait = at(ms * MS);
	siginfo_t si;
	return sigtimedwait(&set, &si, &wait) == SIGUSR1 && si.si_code == SI_TIMER;
}

static void absolute(clockid_t clock)
{
	struct sigevent sev = {.sigev_notify = SIGEV_SIGNAL, .sigev_signo = SIGUSR1};
	timer_t t;
	CHECK(timer_create(clock, &sev, &t) == 0);

	/* In the past: at once. */
	struct itimerspec spec = {.it_value = at(1)};
	CHECK(timer_settime(t, TIMER_ABSTIME, &spec, NULL) == 0);
	CHECK(took(0));

	/* 30 ms ahead. */
	long long start = now(CLOCK_MONOTONIC);
	spec.it_value = at(now(clock) + 30 * MS);
	CHECK(timer_settime(t, TIMER_ABSTIME, &spec, NULL) == 0);
	struct itimerspec cur;
	CHECK(timer_gettime(t, &cur) == 0);
	CHECK(ns(cur.it_value) > 0 && ns(cur.it_value) <= 30 * MS);
	CHECK(!took(5));
	CHECK(took(2000));
	CHECK(now(CLOCK_MONOTONIC) - start >= 25 * MS);

	/* Absolute first expiry, then the interval. */
	spec = (struct itimerspec){.it_value = at(now(clock) + 5 * MS), .it_interval = at(5 * MS)};
	CHECK(timer_settime(t, TIMER_ABSTIME, &spec, NULL) == 0);
	CHECK(took(2000));
	CHECK(took(2000));
	CHECK(timer_delete(t) == 0);
}

int main(void)
{
	sigemptyset(&set);
	sigaddset(&set, SIGUSR1);
	CHECK(sigprocmask(SIG_BLOCK, &set, NULL) == 0);

	absolute(CLOCK_REALTIME);
	absolute(CLOCK_MONOTONIC);
	absolute(CLOCK_BOOTTIME);

	/* A relative CPU-time timer. */
	struct sigevent sev = {.sigev_notify = SIGEV_SIGNAL, .sigev_signo = SIGUSR1};
	timer_t t;
	CHECK(timer_create(CLOCK_PROCESS_CPUTIME_ID, &sev, &t) == 0);
	struct itimerspec spec = {.it_value = at(10 * MS)};
	CHECK(timer_settime(t, 0, &spec, NULL) == 0);
	/* Burn CPU: it counts as CPU time wherever CPU time is measured. */
	long long until = now(CLOCK_MONOTONIC) + 20 * MS;
	while (now(CLOCK_MONOTONIC) < until)
		;
	CHECK(took(2000));
	CHECK(timer_delete(t) == 0);

	if (failures)
		return 1;
	puts("ok");
	return 0;
}
