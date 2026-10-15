// alarm() delivers SIGALRM after the delay, and pause() returns -1 EINTR once the handler ran.
// A second alarm() returns the seconds left on the first and replaces it.
//#ExpectedStdout: left 5
//#ExpectedStdout: alarm 14
//#ExpectedStdout: pause -1 EINTR
//#ExpectedExitCode: 0
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

static void handler(int sig)
{
	printf("alarm %d\n", sig);
}

int main(void)
{
	signal(SIGALRM, handler);
	alarm(5);
	printf("left %u\n", alarm(1));
	int r = pause();
	printf("pause %d %s\n", r, errno == EINTR ? "EINTR" : "?");
	return 0;
}
