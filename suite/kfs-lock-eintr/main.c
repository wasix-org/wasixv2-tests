// The lock-with-timeout idiom: alarm() and then a blocking flock or F_OFD_SETLKW on a file
// another open file holds locked. The wait ends with EINTR once the handler ran.
//#ExpectedStdout: flock -1 EINTR
//#ExpectedStdout: setlkw -1 EINTR
//#ExpectedExitCode: 0
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>

static void handler(int sig)
{
	(void)sig;
}

int main(void)
{
	struct sigaction sa;
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = handler;
	sigaction(SIGALRM, &sa, 0);

	int first = open("/tmp/kfs-lock", O_CREAT | O_RDWR, 0600);
	int second = open("/tmp/kfs-lock", O_RDWR);
	if (first < 0 || second < 0) return 1;

	if (flock(first, LOCK_EX)) return 2;
	alarm(1);
	int r = flock(second, LOCK_EX);
	printf("flock %d %s\n", r, errno == EINTR ? "EINTR" : strerror(errno));

	struct flock lock = {.l_type = F_WRLCK, .l_whence = SEEK_SET};
	if (fcntl(first, F_OFD_SETLK, &lock)) return 3;
	alarm(1);
	r = fcntl(second, F_OFD_SETLKW, &lock);
	printf("setlkw %d %s\n", r, errno == EINTR ? "EINTR" : strerror(errno));
	return 0;
}
