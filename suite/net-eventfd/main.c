// eventfd: counter semantics, EFD_SEMAPHORE, non-blocking EAGAIN, overflow, buffers that are not
// 8 bytes and poll readiness.
//#ExpectedStdout: ok
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
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
	int e = eventfd(3, EFD_NONBLOCK | EFD_CLOEXEC);
	CHECK(e >= 0);
	CHECK(fcntl(e, F_GETFL) & O_NONBLOCK);
	CHECK(fcntl(e, F_GETFD) & FD_CLOEXEC);

	uint64_t v = 4;
	CHECK(write(e, &v, sizeof v) == 8);
	CHECK(read(e, &v, sizeof v) == 8 && v == 7);
	CHECK(read(e, &v, sizeof v) == -1 && errno == EAGAIN);

	// eventfd_read and eventfd_write are libc wrappers over the same thing.
	CHECK(eventfd_write(e, 5) == 0);
	eventfd_t got;
	CHECK(eventfd_read(e, &got) == 0 && got == 5);

	// Buffers shorter than 8 bytes are EINVAL, and so is a write of more than 8.
	CHECK(read(e, &v, 4) == -1 && errno == EINVAL);
	CHECK(write(e, &v, 4) == -1 && errno == EINVAL);
	uint64_t two[2] = { 1, 1 };
	CHECK(write(e, two, sizeof two) == -1 && errno == EINVAL);
	CHECK(read(e, &v, sizeof v) == -1 && errno == EAGAIN);

	// 2^64-1 is never a valid increment.
	v = UINT64_MAX;
	CHECK(write(e, &v, sizeof v) == -1 && errno == EINVAL);

	// The counter tops out at 2^64-2: a write that would overflow is EAGAIN when non-blocking.
	v = UINT64_MAX - 1;
	CHECK(write(e, &v, sizeof v) == 8);
	v = 1;
	CHECK(write(e, &v, sizeof v) == -1 && errno == EAGAIN);

	struct pollfd pfd = { .fd = e, .events = POLLIN | POLLOUT };
	CHECK(poll(&pfd, 1, 0) == 1 && pfd.revents == POLLIN);
	CHECK(read(e, &v, sizeof v) == 8 && v == UINT64_MAX - 1);
	CHECK(poll(&pfd, 1, 0) == 1 && pfd.revents == POLLOUT);
	close(e);

	// Semaphore mode: each read takes one.
	int s = eventfd(2, EFD_SEMAPHORE | EFD_NONBLOCK);
	CHECK(s >= 0);
	CHECK(read(s, &v, sizeof v) == 8 && v == 1);
	CHECK(read(s, &v, sizeof v) == 8 && v == 1);
	CHECK(read(s, &v, sizeof v) == -1 && errno == EAGAIN);
	close(s);

	CHECK(eventfd(0, 0x12345) == -1 && errno == EINVAL);
	printf("ok\n");
	return 0;
}
