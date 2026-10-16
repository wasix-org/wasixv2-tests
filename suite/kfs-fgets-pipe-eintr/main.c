// stdio reads of more than one byte go through readv: fread on an empty pipe is interrupted
// by SIGALRM from a handler without SA_RESTART, so it returns 0 with the error flag and EINTR
// once the handler ran. With SA_RESTART the readv restarts and gets what the handler wrote.
// fgets, which reads byte by byte through read(2), is interrupted the same way.
//#ExpectedStdout: handler 14
//#ExpectedStdout: fread 0 error EINTR
//#ExpectedStdout: handler 14
//#ExpectedStdout: fgets NULL EINTR
//#ExpectedStdout: handler 14
//#ExpectedStdout: restarted 6: hello
//#ExpectedExitCode: 0
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int wfd;

static void handler(int sig)
{
	printf("handler %d\n", sig);
}

static void writer(int sig)
{
	printf("handler %d\n", sig);
	write(wfd, "hello\n", 6);
	close(wfd);
}

static void on_alarm(void (*fn)(int), int flags)
{
	struct sigaction sa;
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = fn;
	sa.sa_flags = flags;
	sigaction(SIGALRM, &sa, 0);
	alarm(1);
}

int main(void)
{
	int fds[2];
	if (pipe(fds)) return 1;
	FILE *in = fdopen(fds[0], "r");
	if (!in) return 2;
	setvbuf(stdout, NULL, _IONBF, 0);
	char buf[32];

	on_alarm(handler, 0);
	size_t n = fread(buf, 1, sizeof buf, in);
	printf("fread %zu %s %s\n", n, ferror(in) ? "error" : "?", errno == EINTR ? "EINTR" : "?");
	clearerr(in);

	on_alarm(handler, 0);
	char *line = fgets(buf, sizeof buf, in);
	printf("fgets %s %s\n", line ? "data" : "NULL", errno == EINTR ? "EINTR" : "?");
	clearerr(in);

	wfd = fds[1];
	on_alarm(writer, SA_RESTART);
	n = fread(buf, 1, sizeof buf, in);
	printf("restarted %zu: %.*s", n, (int)n, buf);
	return 0;
}
