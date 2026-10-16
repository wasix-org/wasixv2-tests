// A sleep is interrupted by a signal that has a handler, with or without SA_RESTART: the
// handler runs and nanosleep fails with EINTR long before the requested time.
//#ExpectedStdout: handler 14
//#ExpectedStdout: nanosleep -1 EINTR early
//#ExpectedStdout: handler 14
//#ExpectedStdout: nanosleep -1 EINTR early
//#ExpectedExitCode: 0
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void handler(int sig)
{
	printf("handler %d\n", sig);
}

static long long now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static void sleep_through_alarm(int flags)
{
	struct sigaction sa;
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = handler;
	sa.sa_flags = flags;
	sigaction(SIGALRM, &sa, 0);
	alarm(1);
	long long start = now();
	struct timespec req = {30, 0};
	errno = 0;
	int r = nanosleep(&req, NULL);
	int early = now() - start < 10 * 1000000000LL;
	printf("nanosleep %d %s %s\n", r, errno == EINTR ? "EINTR" : "?", early ? "early" : "late");
}

int main(void)
{
	sleep_through_alarm(0);
	sleep_through_alarm(SA_RESTART);
	return 0;
}
