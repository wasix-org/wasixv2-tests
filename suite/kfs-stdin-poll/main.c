// Host stdin has real readiness: poll waits until input is there and reports POLLIN, an
// O_NONBLOCK read takes what arrived, and the end of input reads as 0.
//#Stdin: abc
//#ExpectedStdout: poll 1 IN
//#ExpectedStdout: read 3 abc
//#ExpectedStdout: eof 0
//#ExpectedExitCode: 0
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
	struct pollfd pfd = {.fd = 0, .events = POLLIN};
	int n = poll(&pfd, 1, 10000);
	printf("poll %d %s\n", n, pfd.revents & POLLIN ? "IN" : "?");
	fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);
	char buf[16];
	ssize_t got = 0, r;
	// The input may arrive in pieces; take all of it.
	while (got < 3) {
		r = read(0, buf + got, sizeof buf - got);
		if (r > 0) {
			got += r;
		} else if (r < 0 && errno == EAGAIN) {
			poll(&pfd, 1, 10000);
		} else {
			break;
		}
	}
	printf("read %zd %.3s\n", got, buf);
	do {
		poll(&pfd, 1, 10000);
		r = read(0, buf, sizeof buf);
	} while (r < 0 && errno == EAGAIN);
	printf("eof %zd\n", r);
	return 0;
}
