// RLIMIT_NOFILE on every other way to get a descriptor: dup2 and F_DUPFD at or above the
// limit, and calls that create descriptors (pipe, eventfd, socket, epoll, timerfd).
//#ExpectedStdout: ok
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

static int failures;
#define CHECK(cond)                                                                       \
	do {                                                                              \
		if (!(cond)) {                                                            \
			printf("FAIL line %d: %s (errno %d: %s)\n", __LINE__, #cond, errno, \
			       strerror(errno));                                          \
			failures++;                                                       \
		}                                                                         \
	} while (0)
#define CHECK_ERR(expr, err)                                                              \
	do {                                                                              \
		errno = 0;                                                                \
		long r_ = (long)(expr);                                                   \
		if (r_ != -1 || errno != (err)) {                                         \
			printf("FAIL line %d: %s = %ld errno %d, want %s\n", __LINE__, #expr, \
			       r_, errno, #err);                                          \
			failures++;                                                       \
		}                                                                         \
	} while (0)

#define LIMIT 8

int main(void)
{
	struct rlimit rl;
	CHECK(getrlimit(RLIMIT_NOFILE, &rl) == 0);
	rl.rlim_cur = LIMIT;
	CHECK(setrlimit(RLIMIT_NOFILE, &rl) == 0);

	CHECK_ERR(dup2(0, LIMIT), EBADF);
	CHECK_ERR(dup3(0, LIMIT + 5, O_CLOEXEC), EBADF);
	CHECK_ERR(fcntl(0, F_DUPFD, LIMIT), EINVAL);
	CHECK_ERR(fcntl(0, F_DUPFD_CLOEXEC, LIMIT), EINVAL);
	CHECK(dup2(0, LIMIT - 1) == LIMIT - 1);

	/* Fill the table: fds 3 .. LIMIT - 1 are taken. */
	for (int fd = 3; fd < LIMIT - 1; fd++)
		CHECK(dup(0) == fd);
	int fds[2];
	CHECK_ERR(pipe(fds), EMFILE);
	CHECK_ERR(eventfd(0, 0), EMFILE);
	CHECK_ERR(socket(AF_UNIX, SOCK_STREAM, 0), EMFILE);
	CHECK_ERR(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), EMFILE);
	CHECK_ERR(epoll_create1(0), EMFILE);
	CHECK_ERR(timerfd_create(CLOCK_MONOTONIC, 0), EMFILE);

	/* A pipe needs two free descriptors. */
	close(LIMIT - 1);
	CHECK_ERR(pipe(fds), EMFILE);
	CHECK(dup(0) == LIMIT - 1);

	if (failures)
		return 1;
	puts("ok");
	return 0;
}
