// epoll_wait into an array the kernel cannot write fails with EFAULT and loses nothing: a
// one-shot item stays armed and an edge-triggered event stays ready for the next wait, as
// Linux's ep_send_events puts items whose copy-out failed back on the ready list.
//#ExpectedStdout: fault -1 EFAULT
//#ExpectedStdout: next 2
//#ExpectedStdout: again 0
//#ExpectedExitCode: 0
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/epoll.h>
#include <unistd.h>

int main(void)
{
	int a[2], b[2];
	if (pipe(a) || pipe(b))
		return 1;
	int ep = epoll_create1(0);
	struct epoll_event ev = { .events = EPOLLIN | EPOLLONESHOT, .data.fd = a[0] };
	epoll_ctl(ep, EPOLL_CTL_ADD, a[0], &ev);
	ev = (struct epoll_event){ .events = EPOLLIN | EPOLLET, .data.fd = b[0] };
	epoll_ctl(ep, EPOLL_CTL_ADD, b[0], &ev);
	write(a[1], "x", 1);
	write(b[1], "x", 1);

	/* Far past the end of linear memory, but inside the 32-bit address space. */
	struct epoll_event *bad = (struct epoll_event *)(uintptr_t)0xf0000000u;
	errno = 0;
	int n = epoll_wait(ep, bad, 8, 0);
	printf("fault %d %s\n", n, errno == EFAULT ? "EFAULT" : "?");
	struct epoll_event out[8];
	printf("next %d\n", epoll_wait(ep, out, 8, 0));
	printf("again %d\n", epoll_wait(ep, out, 8, 0));
	return 0;
}
