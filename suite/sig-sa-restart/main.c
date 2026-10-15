// With SA_RESTART, a read interrupted by a handled signal restarts instead of failing: the
// handler runs, then the read keeps waiting and returns the byte written later (the interval
// timer's second tick writes it from the handler).
//#ExpectedStdout: tick 1
//#ExpectedStdout: tick 2
//#ExpectedStdout: read 1 x
//#ExpectedExitCode: 0
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

static int fds[2];
static volatile int ticks;

static void handler(int sig)
{
	ticks++;
	printf("tick %d\n", ticks);
	if (ticks == 2) {
		struct itimerval off = { 0 };
		setitimer(ITIMER_REAL, &off, 0);
		write(fds[1], "x", 1);
	}
}

int main(void)
{
	struct sigaction sa;
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = handler;
	sa.sa_flags = SA_RESTART;
	sigaction(SIGALRM, &sa, 0);
	if (pipe(fds)) return 1;
	struct itimerval it = { { 0, 100000 }, { 0, 100000 } };
	setitimer(ITIMER_REAL, &it, 0);
	char c = 0;
	ssize_t n = read(fds[0], &c, 1);
	printf("read %zd %c\n", n, c);
	return 0;
}
