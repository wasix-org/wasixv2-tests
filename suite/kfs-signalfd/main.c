// signalfd: blocked signals are read as signalfd_siginfo records instead of running a
// handler; the descriptor polls readable while one is pending, a non-blocking read with none
// fails with EAGAIN, sigqueue's value arrives in ssi_int, and signalfd on an existing
// descriptor replaces its mask.
//#ExpectedStdout: ok
#define _GNU_SOURCE
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
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

static int readable(int fd)
{
	struct pollfd pfd = {.fd = fd, .events = POLLIN};
	return poll(&pfd, 1, 0) == 1 && (pfd.revents & POLLIN);
}

int main(void)
{
	sigset_t mask;
	sigemptyset(&mask);
	sigaddset(&mask, SIGUSR1);
	sigaddset(&mask, SIGRTMIN);
	sigprocmask(SIG_BLOCK, &mask, NULL);

	int fd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
	CHECK(fd >= 0);
	struct signalfd_siginfo si;
	CHECK(read(fd, &si, sizeof si) == -1 && errno == EAGAIN);
	CHECK(read(fd, &si, sizeof si - 1) == -1 && errno == EINVAL);
	CHECK(!readable(fd));

	CHECK(kill(getpid(), SIGUSR1) == 0);
	CHECK(readable(fd));
	int ep = epoll_create1(0);
	struct epoll_event ev = {.events = EPOLLIN};
	CHECK(epoll_ctl(ep, EPOLL_CTL_ADD, fd, &ev) == 0);
	CHECK(epoll_wait(ep, &ev, 1, 0) == 1);
	CHECK(read(fd, &si, sizeof si) == sizeof si);
	CHECK(si.ssi_signo == SIGUSR1);
	CHECK(si.ssi_pid == (unsigned)getpid());
	CHECK(si.ssi_code == SI_USER);
	CHECK(!readable(fd));

	// Two records at once: a queued real-time signal and a plain one.
	union sigval value = {.sival_int = 42};
	CHECK(sigqueue(getpid(), SIGRTMIN, value) == 0);
	CHECK(kill(getpid(), SIGUSR1) == 0);
	struct signalfd_siginfo two[2];
	CHECK(read(fd, two, sizeof two) == sizeof two);
	CHECK(two[0].ssi_signo == SIGUSR1);
	CHECK(two[1].ssi_signo == (unsigned)SIGRTMIN && two[1].ssi_int == 42);
	CHECK(two[1].ssi_code == SI_QUEUE);

	// A new mask without SIGUSR1: the pending signal is no longer readable.
	sigset_t rt;
	sigemptyset(&rt);
	sigaddset(&rt, SIGRTMIN);
	CHECK(signalfd(fd, &rt, 0) == fd);
	CHECK(kill(getpid(), SIGUSR1) == 0);
	CHECK(!readable(fd));
	CHECK(signalfd(fd, &mask, 0) == fd);
	CHECK(readable(fd));

	CHECK(signalfd(0, &mask, 0) == -1 && errno == EINVAL);
	CHECK(signalfd(-1, &mask, 0x1) == -1 && errno == EINVAL);
	if (!failures) printf("ok\n");
	return failures != 0;
}
