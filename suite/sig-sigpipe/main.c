// Writing to a pipe without readers raises SIGPIPE and fails with EPIPE; with SIGPIPE ignored
// only EPIPE remains. By default SIGPIPE kills the process: 128 + 13.
//#ExpectedStdout: handler 13 write -1 EPIPE
//#ExpectedStdout: ignored write -1 EPIPE
//#ExpectedExitCode: 141
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

static volatile int got;
static void handler(int sig) { got = sig; }

int main(void)
{
	int fds[2];
	if (pipe(fds)) return 1;
	close(fds[0]);
	signal(SIGPIPE, handler);
	ssize_t n = write(fds[1], "x", 1);
	printf("handler %d write %zd %s\n", got, n, errno == EPIPE ? "EPIPE" : "?");
	signal(SIGPIPE, SIG_IGN);
	n = write(fds[1], "x", 1);
	printf("ignored write %zd %s\n", n, errno == EPIPE ? "EPIPE" : "?");
	fflush(stdout);
	signal(SIGPIPE, SIG_DFL);
	write(fds[1], "x", 1);
	return 0;
}
