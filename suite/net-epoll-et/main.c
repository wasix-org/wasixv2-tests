// Edge-triggered epoll on a pipe: an edge is reported once, again only after new data arrives,
// and level-triggered items keep reporting until drained.
//#ExpectedStdout: ok
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
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
	CHECK(pipe(p) == 0);
	int ep = epoll_create1(EPOLL_CLOEXEC);
	CHECK(ep >= 0);

	struct epoll_event ev = { .events = EPOLLIN | EPOLLET, .data.u64 = 0x1122334455667788ull };
	CHECK(epoll_ctl(ep, EPOLL_CTL_ADD, p[0], &ev) == 0);
	CHECK(epoll_ctl(ep, EPOLL_CTL_ADD, p[0], &ev) == -1 && errno == EEXIST);

	struct epoll_event out[4];
	CHECK(epoll_wait(ep, out, 4, 0) == 0);

	CHECK(write(p[1], "ab", 2) == 2);
	CHECK(epoll_wait(ep, out, 4, 0) == 1);
	CHECK(out[0].events == EPOLLIN && out[0].data.u64 == 0x1122334455667788ull);
	// No new edge: nothing, although data is still there.
	CHECK(epoll_wait(ep, out, 4, 0) == 0);

	// Reading part of it creates no edge either.
	char c;
	CHECK(read(p[0], &c, 1) == 1);
	CHECK(epoll_wait(ep, out, 4, 0) == 0);

	// New data is a new edge.
	CHECK(write(p[1], "c", 1) == 1);
	CHECK(epoll_wait(ep, out, 4, 0) == 1 && out[0].events == EPOLLIN);

	// Level-triggered: reported on every wait while readable.
	ev.events = EPOLLIN;
	ev.data.fd = p[0];
	CHECK(epoll_ctl(ep, EPOLL_CTL_MOD, p[0], &ev) == 0);
	CHECK(epoll_wait(ep, out, 4, 0) == 1 && out[0].data.fd == p[0]);
	CHECK(epoll_wait(ep, out, 4, 0) == 1);
	char buf[8];
	CHECK(read(p[0], buf, sizeof buf) == 2);
	CHECK(epoll_wait(ep, out, 4, 0) == 0);

	// The write end: writable, and EPOLLERR once the reader is gone.
	ev.events = EPOLLOUT | EPOLLET;
	ev.data.fd = p[1];
	CHECK(epoll_ctl(ep, EPOLL_CTL_ADD, p[1], &ev) == 0);
	CHECK(epoll_wait(ep, out, 4, 0) == 1 && out[0].data.fd == p[1] &&
	      out[0].events == EPOLLOUT);

	// Closing the writer: the reader sees EPOLLHUP (always reported).
	CHECK(epoll_ctl(ep, EPOLL_CTL_DEL, p[1], NULL) == 0);
	CHECK(epoll_ctl(ep, EPOLL_CTL_DEL, p[1], NULL) == -1 && errno == ENOENT);
	close(p[1]);
	CHECK(epoll_wait(ep, out, 4, 0) == 1 && out[0].data.fd == p[0]);
	CHECK(out[0].events == EPOLLHUP);

	// A timeout with nothing ready returns 0 after waiting.
	CHECK(epoll_ctl(ep, EPOLL_CTL_DEL, p[0], NULL) == 0);
	CHECK(epoll_wait(ep, out, 4, 20) == 0);

	// Errors.
	CHECK(epoll_wait(ep, out, 0, 0) == -1 && errno == EINVAL);
	CHECK(epoll_ctl(ep, EPOLL_CTL_ADD, ep, &ev) == -1 && errno == EINVAL);
	CHECK(epoll_ctl(ep, EPOLL_CTL_ADD, 999, &ev) == -1 && errno == EBADF);
	printf("ok\n");
	return 0;
}
