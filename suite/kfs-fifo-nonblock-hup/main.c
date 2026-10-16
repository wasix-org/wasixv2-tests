// A FIFO opened O_RDONLY|O_NONBLOCK before any writer polls neither readable nor hung up, so a
// daemon waiting for its first client does not spin. Once a writer has come and gone, the
// read end hangs up.
//#ExpectedStdout: before 0
//#ExpectedStdout: with writer 0
//#ExpectedStdout: data 1 IN
//#ExpectedStdout: after 1 HUP
//#ExpectedExitCode: 0
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

static int check(int fd, short *revents)
{
	struct pollfd pfd = {.fd = fd, .events = POLLIN};
	int n = poll(&pfd, 1, 0);
	*revents = pfd.revents;
	return n;
}

int main(void)
{
	if (mkfifo("/tmp/kfs-fifo", 0600)) return 1;
	int r = open("/tmp/kfs-fifo", O_RDONLY | O_NONBLOCK);
	if (r < 0) return 2;
	short revents;
	printf("before %d\n", check(r, &revents));
	int w = open("/tmp/kfs-fifo", O_WRONLY | O_NONBLOCK);
	if (w < 0) return 3;
	printf("with writer %d\n", check(r, &revents));
	write(w, "x", 1);
	int n = check(r, &revents);
	printf("data %d %s\n", n, revents == POLLIN ? "IN" : "?");
	char c;
	read(r, &c, 1);
	close(w);
	n = check(r, &revents);
	printf("after %d %s\n", n, revents == POLLHUP ? "HUP" : "?");
	return 0;
}
