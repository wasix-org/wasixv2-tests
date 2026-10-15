// poll and ppoll on pipes: readiness, POLLHUP after the writer closes, POLLERR after the reader
// closes, POLLNVAL for a closed descriptor, negative descriptors ignored, and timeouts.
//#ExpectedStdout: ok
#define _GNU_SOURCE
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define CHECK(c)                                                                     \
	do {                                                                         \
		if (!(c)) {                                                          \
			printf("FAIL %s:%d: %s (errno=%d %s)\n", __FILE__, __LINE__, \
			       #c, errno, strerror(errno));                          \
			exit(1);                                                     \
		}                                                                    \
	} while (0)

static long ms_since(struct timespec *a)
{
	struct timespec b;
	clock_gettime(CLOCK_MONOTONIC, &b);
	return (b.tv_sec - a->tv_sec) * 1000 + (b.tv_nsec - a->tv_nsec) / 1000000;
}

int main(void)
{
	int p[2];
	CHECK(pipe(p) == 0);

	struct pollfd fds[3] = {
		{ .fd = p[0], .events = POLLIN },
		{ .fd = p[1], .events = POLLOUT },
		{ .fd = -1, .events = POLLIN, .revents = 123 },
	};
	CHECK(poll(fds, 3, 0) == 1);
	CHECK(fds[0].revents == 0 && fds[1].revents == POLLOUT && fds[2].revents == 0);

	// Only the requested events, plus the always-reported ones.
	fds[1].events = POLLIN;
	CHECK(poll(fds, 2, 0) == 0);

	struct timespec start;
	clock_gettime(CLOCK_MONOTONIC, &start);
	CHECK(poll(fds, 1, 50) == 0);
	CHECK(ms_since(&start) >= 45);

	CHECK(write(p[1], "x", 1) == 1);
	CHECK(poll(fds, 1, -1) == 1 && fds[0].revents == POLLIN);

	// ppoll writes nothing back to a const timeout and accepts NULL for an infinite one.
	struct timespec ts = { 0, 0 };
	CHECK(ppoll(fds, 1, &ts, NULL) == 1 && fds[0].revents == POLLIN);
	CHECK(ppoll(fds, 1, NULL, NULL) == 1);

	close(p[1]);
	CHECK(poll(fds, 1, 0) == 1 && fds[0].revents == (POLLIN | POLLHUP));
	char c;
	CHECK(read(p[0], &c, 1) == 1);
	CHECK(poll(fds, 1, 0) == 1 && fds[0].revents == POLLHUP);
	// POLLHUP is reported even if not asked for.
	fds[0].events = 0;
	CHECK(poll(fds, 1, 0) == 1 && fds[0].revents == POLLHUP);

	// The reader gone: the writer sees POLLERR.
	int q[2];
	CHECK(pipe(q) == 0);
	close(q[0]);
	struct pollfd w = { .fd = q[1], .events = POLLOUT };
	CHECK(poll(&w, 1, 0) == 1 && w.revents == (POLLOUT | POLLERR));

	// A closed descriptor: POLLNVAL, counted.
	close(q[1]);
	struct pollfd bad = { .fd = q[1], .events = POLLIN };
	CHECK(poll(&bad, 1, 0) == 1 && bad.revents == POLLNVAL);

	// Too many descriptors.
	volatile nfds_t many = 1u << 30;
	CHECK(poll(fds, many, 0) == -1 && errno == EINVAL);
	printf("ok\n");
	return 0;
}
