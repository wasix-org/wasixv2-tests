// dup/dup2/dup3 and fcntl F_DUPFD, F_DUPFD_CLOEXEC, F_GETFD/F_SETFD, F_GETFL/F_SETFL.
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: CHECK(%s) failed (errno %s)\n", __LINE__, #c, strerror(errno)); fails++; } } while (0)
#define OK(call) do { errno = 0; if ((long)(call) < 0) { fprintf(stderr, "line %d: %s failed: %s\n", __LINE__, #call, strerror(errno)); fails++; } } while (0)
#define FAILS(call, e) do { errno = 0; long r_ = (long)(call); if (r_ != -1 || errno != (e)) { fprintf(stderr, "line %d: %s = %ld (%s), want -1 %s\n", __LINE__, #call, r_, strerror(errno), #e); fails++; } } while (0)

int main(void)
{
	int fd = open("/tmp/f", O_CREAT | O_RDWR | O_CLOEXEC, 0644);
	CHECK(fd == 3);
	int d = dup(fd);
	CHECK(d == 4);
	CHECK(fcntl(d, F_GETFD) == 0);
	/* Shared offset and status flags. */
	CHECK(write(fd, "abc", 3) == 3);
	CHECK(lseek(d, 0, SEEK_CUR) == 3);
	OK(fcntl(d, F_SETFL, O_APPEND));
	CHECK(fcntl(fd, F_GETFL) & O_APPEND);

	CHECK(dup2(fd, fd) == fd);
	FAILS(dup2(fd, -1), EBADF);
	FAILS(dup2(99, 10), EBADF);
	CHECK(dup2(fd, 10) == 10);
	CHECK(dup2(d, 10) == 10);
	CHECK(fcntl(10, F_GETFD) == 0);
	FAILS(dup3(fd, fd, 0), EINVAL);
	FAILS(dup3(fd, 11, 0x1), EINVAL);
	CHECK(dup3(fd, 11, O_CLOEXEC) == 11);
	CHECK(fcntl(11, F_GETFD) == FD_CLOEXEC);

	CHECK(fcntl(fd, F_DUPFD, 20) == 20);
	CHECK(fcntl(fd, F_DUPFD, 20) == 21);
	CHECK(fcntl(fd, F_DUPFD_CLOEXEC, 0) == 5);
	CHECK(fcntl(5, F_GETFD) == FD_CLOEXEC);
	FAILS(fcntl(fd, F_DUPFD, -1), EINVAL);
	FAILS(fcntl(fd, F_DUPFD, 1 << 20), EINVAL);

	OK(fcntl(5, F_SETFD, 0));
	CHECK(fcntl(5, F_GETFD) == 0);
	OK(fcntl(5, F_SETFD, FD_CLOEXEC));
	CHECK(fcntl(5, F_GETFD) == FD_CLOEXEC);

	/* F_SETFL changes O_APPEND and O_NONBLOCK, never the access mode. */
	OK(fcntl(fd, F_SETFL, O_RDONLY | O_NONBLOCK));
	int fl = fcntl(fd, F_GETFL);
	CHECK((fl & O_ACCMODE) == O_RDWR);
	CHECK(fl & O_NONBLOCK);
	CHECK(!(fl & O_APPEND));

	FAILS(fcntl(99, F_GETFD), EBADF);
	FAILS(fcntl(fd, 9999), EINVAL);
	FAILS(close(99), EBADF);
	OK(close(10));
	FAILS(close(10), EBADF);

	/* Lowest free slot. */
	OK(close(4));
	CHECK(dup(fd) == 4);

	if (fails)
		return 1;
	puts("PASS");
	return 0;
}
