// A read blocked on an empty pipe is interrupted by a signal whose handler lacks SA_RESTART:
// the handler runs and read fails with EINTR.
//#ExpectedStdout: handler 14
//#ExpectedStdout: read -1 EINTR
//#ExpectedExitCode: 0
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void handler(int sig)
{
	printf("handler %d\n", sig);
}

int main(void)
{
	struct sigaction sa;
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = handler;
	sigaction(SIGALRM, &sa, 0);
	int fds[2];
	if (pipe(fds)) return 1;
	alarm(1);
	char c;
	ssize_t n = read(fds[0], &c, 1);
	printf("read %zd %s\n", n, errno == EINTR ? "EINTR" : "?");
	return 0;
}
