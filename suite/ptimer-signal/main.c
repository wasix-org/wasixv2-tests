// POSIX timers with SIGEV_SIGNAL: a one-shot timer runs its handler once with SI_TIMER, the
// timer id and the sigevent value; an interval timer keeps firing until disarmed; gettime counts
// down and settime returns the old setting; the default sigevent is SIGALRM carrying the id.
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

static volatile sig_atomic_t hits, code, timerid, value, overrun;

static void handler(int sig, siginfo_t *si, void *ctx)
{
	(void)sig;
	(void)ctx;
	hits++;
	code = si->si_code;
	timerid = si->si_timerid;
	value = si->si_value.sival_int;
	overrun = si->si_overrun;
}

static struct timespec at(long long ns)
{
	return (struct timespec){ns / 1000000000LL, ns % 1000000000LL};
}

static long long ns(struct timespec ts)
{
	return ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static void wait_hits(int n)
{
	for (int i = 0; i < 2000 && hits < n; i++)
		usleep(1000);
}

int main(void)
{
	struct sigaction sa = {.sa_sigaction = handler, .sa_flags = SA_SIGINFO};
	sigemptyset(&sa.sa_mask);
	CHECK(sigaction(SIGUSR1, &sa, NULL) == 0);
	CHECK(sigaction(SIGALRM, &sa, NULL) == 0);

	/* One-shot. */
	struct sigevent sev = {.sigev_notify = SIGEV_SIGNAL, .sigev_signo = SIGUSR1};
	sev.sigev_value.sival_int = 4242;
	timer_t t;
	CHECK(timer_create(CLOCK_MONOTONIC, &sev, &t) == 0);
	struct itimerspec cur;
	CHECK(timer_gettime(t, &cur) == 0);
	CHECK(ns(cur.it_value) == 0 && ns(cur.it_interval) == 0);

	struct itimerspec spec = {.it_value = at(20 * MS)};
	CHECK(timer_settime(t, 0, &spec, NULL) == 0);
	CHECK(timer_gettime(t, &cur) == 0);
	CHECK(ns(cur.it_value) > 0 && ns(cur.it_value) <= 20 * MS);
	wait_hits(1);
	CHECK(hits == 1);
	CHECK(code == SI_TIMER);
	CHECK(timerid == (int)(long)t);
	CHECK(value == 4242);
	CHECK(overrun == 0);
	CHECK(timer_gettime(t, &cur) == 0);
	CHECK(ns(cur.it_value) == 0);
	usleep(40000);
	CHECK(hits == 1);

	/* Interval, then the old value from settime, then disarming. */
	hits = 0;
	spec = (struct itimerspec){.it_value = at(10 * MS), .it_interval = at(10 * MS)};
	CHECK(timer_settime(t, 0, &spec, NULL) == 0);
	wait_hits(4);
	CHECK(hits >= 4);
	CHECK(timer_gettime(t, &cur) == 0);
	CHECK(ns(cur.it_interval) == 10 * MS);
	CHECK(ns(cur.it_value) > 0 && ns(cur.it_value) <= 10 * MS);
	struct itimerspec old;
	spec = (struct itimerspec){0};
	CHECK(timer_settime(t, 0, &spec, &old) == 0);
	CHECK(ns(old.it_interval) == 10 * MS);
	CHECK(ns(old.it_value) > 0 && ns(old.it_value) <= 10 * MS);
	int seen = hits;
	usleep(50000);
	CHECK(hits == seen);
	CHECK(timer_delete(t) == 0);

	/* A null sigevent: SIGALRM with the timer id as the value. */
	hits = 0;
	CHECK(timer_create(CLOCK_REALTIME, NULL, &t) == 0);
	spec = (struct itimerspec){.it_value = at(5 * MS)};
	CHECK(timer_settime(t, 0, &spec, NULL) == 0);
	wait_hits(1);
	CHECK(hits == 1 && code == SI_TIMER);
	CHECK(value == (int)(long)t && timerid == (int)(long)t);
	CHECK(timer_delete(t) == 0);

	if (failures)
		return 1;
	puts("ok");
	return 0;
}
