// Signal-driven I/O on sockets. A socket takes its owner through FIOSETOWN too and reports it
// through SIOCGPGRP. A Unix stream socket with O_ASYNC signals data (POLL_IN) and its peer's
// close (POLL_HUP); on loopback TCP, a listener signals a connection to accept and a
// connected socket the data that arrives.
//#ExpectedStdout: owner 1
//#ExpectedStdout: unix POLL_IN 1
//#ExpectedStdout: unix POLL_HUP 1
//#ExpectedStdout: tcp accept POLL_IN 1
//#ExpectedStdout: tcp data POLL_IN 1
//#ExpectedExitCode: 0
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static int rt;

/// Waits up to five seconds for the realtime signal and prints its code and whether it names
/// `fd`.
static void show(const char *what, int fd)
{
	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, rt);
	struct timespec five = { 5, 0 };
	siginfo_t si;
	if (sigtimedwait(&set, &si, &five) != rt) {
		printf("%s none\n", what);
		return;
	}
	const char *code = si.si_code == POLL_IN ? "POLL_IN" : si.si_code == POLL_HUP ? "POLL_HUP" : "?";
	printf("%s %s %d\n", what, code, si.si_fd == fd);
}

static void make_async(int fd)
{
	int pid = getpid();
	ioctl(fd, FIOSETOWN, &pid);
	fcntl(fd, F_SETSIG, rt);
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_ASYNC);
}

int main(void)
{
	rt = SIGRTMIN;
	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, rt);
	sigprocmask(SIG_BLOCK, &set, 0);

	int sv[2];
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv))
		return 1;
	make_async(sv[0]);
	int owner = 0;
	ioctl(sv[0], SIOCGPGRP, &owner);
	printf("owner %d\n", owner == getpid());
	write(sv[1], "hi", 2);
	show("unix", sv[0]);
	close(sv[1]);
	show("unix", sv[0]);

	int listener = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in addr = { .sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
	socklen_t len = sizeof addr;
	if (bind(listener, (struct sockaddr *)&addr, sizeof addr) || listen(listener, 1))
		return 2;
	getsockname(listener, (struct sockaddr *)&addr, &len);
	make_async(listener);
	int client = socket(AF_INET, SOCK_STREAM, 0);
	if (connect(client, (struct sockaddr *)&addr, sizeof addr))
		return 3;
	show("tcp accept", listener);
	int server = accept(listener, 0, 0);
	if (server < 0)
		return 4;
	make_async(server);
	write(client, "x", 1);
	show("tcp data", server);
	return 0;
}
