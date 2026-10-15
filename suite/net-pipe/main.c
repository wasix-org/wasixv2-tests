// Pipes: pipe2 flags, FIONREAD, F_GETPIPE_SZ, EOF, EAGAIN on a full non-blocking pipe, and
// partial writes.
//#ExpectedStdout: ok
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(c)                                                                     \
	do {                                                                         \
		if (!(c)) {                                                          \
			printf("FAIL %s:%d: %s (errno=%d %s)\n", __FILE__, __LINE__, \
			       #c, errno, strerror(errno));                          \
			exit(1);                                                     \
		}                                                                    \
	} while (0)

int main(void)
{
	int p[2];
	CHECK(pipe2(p, O_NONBLOCK | O_CLOEXEC) == 0);
	CHECK((fcntl(p[0], F_GETFL) & O_ACCMODE) == O_RDONLY);
	CHECK((fcntl(p[1], F_GETFL) & O_ACCMODE) == O_WRONLY);
	CHECK(fcntl(p[0], F_GETFL) & O_NONBLOCK);
	CHECK(fcntl(p[1], F_GETFD) & FD_CLOEXEC);

	struct stat st;
	CHECK(fstat(p[0], &st) == 0 && S_ISFIFO(st.st_mode));

	char buf[64];
	CHECK(read(p[0], buf, sizeof buf) == -1 && errno == EAGAIN);
	// Wrong direction.
	CHECK(write(p[0], "x", 1) == -1 && errno == EBADF);
	CHECK(read(p[1], buf, 1) == -1 && errno == EBADF);
	// Pipes cannot seek.
	CHECK(lseek(p[0], 0, SEEK_CUR) == -1 && errno == ESPIPE);

	CHECK(write(p[1], "hello", 5) == 5);
	int n = 0;
	CHECK(ioctl(p[0], FIONREAD, &n) == 0 && n == 5);
	CHECK(read(p[0], buf, 3) == 3 && memcmp(buf, "hel", 3) == 0);
	CHECK(read(p[0], buf, sizeof buf) == 2 && memcmp(buf, "lo", 2) == 0);

	int size = fcntl(p[1], F_GETPIPE_SZ);
	CHECK(size == 65536);

	// Fill it: a write that does not fit is partial, then EAGAIN.
	static char big[100000];
	ssize_t w = write(p[1], big, sizeof big);
	CHECK(w == size);
	CHECK(write(p[1], big, 1) == -1 && errno == EAGAIN);
	CHECK(ioctl(p[0], FIONREAD, &n) == 0 && n == size);

	// Drain, close the writer: EOF.
	long total = 0;
	for (;;) {
		ssize_t r = read(p[0], big, sizeof big);
		if (r <= 0)
			break;
		total += r;
	}
	CHECK(total == size);
	close(p[1]);
	CHECK(read(p[0], buf, sizeof buf) == 0);
	close(p[0]);

	// pipe2 rejects unknown flags.
	CHECK(pipe2(p, O_APPEND) == -1 && errno == EINVAL);
	printf("ok\n");
	return 0;
}
