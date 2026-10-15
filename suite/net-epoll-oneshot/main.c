// EPOLLONESHOT: an item reports once and is then disarmed until EPOLL_CTL_MOD re-arms it;
// maxevents limits a wait and the remaining items are reported by the next one.
//#ExpectedStdout: ok
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
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
	int ep = epoll_create1(0);
	CHECK(ep >= 0);
	int e = eventfd(1, 0);
	CHECK(e >= 0);

	struct epoll_event ev = { .events = EPOLLIN | EPOLLONESHOT, .data.u32 = 7 };
	CHECK(epoll_ctl(ep, EPOLL_CTL_ADD, e, &ev) == 0);

	struct epoll_event out[4];
	CHECK(epoll_wait(ep, out, 4, 0) == 1 && out[0].data.u32 == 7);
	// Disarmed: still readable, but not reported.
	CHECK(epoll_wait(ep, out, 4, 0) == 0);
	uint64_t one = 1;
	CHECK(write(e, &one, sizeof one) == sizeof one);
	CHECK(epoll_wait(ep, out, 4, 0) == 0);

	// Re-arm.
	ev.data.u32 = 8;
	CHECK(epoll_ctl(ep, EPOLL_CTL_MOD, e, &ev) == 0);
	CHECK(epoll_wait(ep, out, 4, 0) == 1 && out[0].data.u32 == 8);
	CHECK(epoll_wait(ep, out, 4, 0) == 0);

	// A disarmed item can still be deleted.
	CHECK(epoll_ctl(ep, EPOLL_CTL_DEL, e, NULL) == 0);

	// maxevents: three ready items, two per wait, the third comes next.
	int fds[3];
	for (int i = 0; i < 3; i++) {
		fds[i] = eventfd(1, 0);
		CHECK(fds[i] >= 0);
		struct epoll_event x = { .events = EPOLLIN | EPOLLONESHOT, .data.u32 = 100 + i };
		CHECK(epoll_ctl(ep, EPOLL_CTL_ADD, fds[i], &x) == 0);
	}
	int seen = 0;
	CHECK(epoll_wait(ep, out, 2, 0) == 2);
	seen |= 1 << (out[0].data.u32 - 100);
	seen |= 1 << (out[1].data.u32 - 100);
	CHECK(epoll_wait(ep, out, 2, 0) == 1);
	seen |= 1 << (out[0].data.u32 - 100);
	CHECK(seen == 7);
	CHECK(epoll_wait(ep, out, 2, 0) == 0);

	// EPOLLEXCLUSIVE cannot be combined with ONESHOT, and cannot be used with MOD.
	int f = eventfd(0, 0);
	struct epoll_event x = { .events = EPOLLIN | EPOLLEXCLUSIVE | EPOLLONESHOT };
	CHECK(epoll_ctl(ep, EPOLL_CTL_ADD, f, &x) == -1 && errno == EINVAL);
	x.events = EPOLLIN | EPOLLEXCLUSIVE;
	CHECK(epoll_ctl(ep, EPOLL_CTL_ADD, f, &x) == 0);
	CHECK(epoll_ctl(ep, EPOLL_CTL_MOD, f, &x) == -1 && errno == EINVAL);

	// Closing the only descriptor of an open file removes its item.
	close(f);
	f = eventfd(1, 0);
	CHECK(epoll_wait(ep, out, 4, 0) == 0);
	printf("ok\n");
	return 0;
}
