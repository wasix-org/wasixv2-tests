// Overruns: while an interval timer's signal stays blocked, later expirations are not queued
// again but counted, and si_overrun and timer_getoverrun report them once the signal is taken.
// The timer re-arms at delivery, so the next signal has a small overrun again.
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

int main(void)
{
	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, SIGRTMIN);
	CHECK(sigprocmask(SIG_BLOCK, &set, NULL) == 0);

	struct sigevent sev = {.sigev_notify = SIGEV_SIGNAL, .sigev_signo = SIGRTMIN};
	timer_t t;
	CHECK(timer_create(CLOCK_MONOTONIC, &sev, &t) == 0);
	CHECK(timer_getoverrun(t) == 0);
	struct itimerspec spec = {
		.it_value = {0, 5 * MS},
		.it_interval = {0, 5 * MS},
	};
	CHECK(timer_settime(t, 0, &spec, NULL) == 0);
	usleep(100000);

	/* One signal queued, even for a realtime signal; about 19 periods missed. */
	sigset_t pending;
	CHECK(sigpending(&pending) == 0 && sigismember(&pending, SIGRTMIN));
	siginfo_t si;
	struct timespec zero = {0, 0};
	CHECK(sigtimedwait(&set, &si, &zero) == SIGRTMIN);
	CHECK(si.si_code == SI_TIMER);
	CHECK(si.si_overrun >= 10 && si.si_overrun <= 25);
	CHECK(timer_getoverrun(t) == si.si_overrun);
	errno = 0;
	CHECK(sigtimedwait(&set, &si, &zero) == -1 && errno == EAGAIN);

	/* Re-armed at delivery. */
	int first = timer_getoverrun(t);
	struct timespec wait = {1, 0};
	CHECK(sigtimedwait(&set, &si, &wait) == SIGRTMIN);
	CHECK(si.si_overrun < first);

	/* A new setting resets the count. */
	spec = (struct itimerspec){.it_value = {10, 0}};
	CHECK(timer_settime(t, 0, &spec, NULL) == 0);
	CHECK(timer_getoverrun(t) == 0);
	CHECK(timer_delete(t) == 0);
	errno = 0;
	CHECK(timer_getoverrun(t) == -1 && errno == EINVAL);

	if (failures)
		return 1;
	puts("ok");
	return 0;
}
