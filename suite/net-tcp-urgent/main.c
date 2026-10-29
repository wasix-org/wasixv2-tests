// Urgent data on loopback TCP, BSD's kind: a send with MSG_OOB makes the last byte it sends
// urgent. The receiving socket's owner gets SIGURG (F_SETOWN without O_ASYNC is enough); the
// socket polls POLLPRI until recv(MSG_OOB) reads the byte, once; reads stop at the mark, which
// SIOCATMARK reports, and step over the byte; SIOCINQ counts up to the mark. With F_SETSIG and
// O_ASYNC, the owner also gets that signal with POLL_PRI. With SO_OOBINLINE the byte stays in
// the stream and MSG_OOB finds nothing.
//#ExpectedStdout: sigurg 1
//#ExpectedStdout: pollpri 1
//#ExpectedStdout: inq 2
//#ExpectedStdout: read ab
//#ExpectedStdout: atmark 1
//#ExpectedStdout: oob U
//#ExpectedStdout: oob again EINVAL
//#ExpectedStdout: pollpri 0
//#ExpectedStdout: read cd
//#ExpectedStdout: setsig POLL_PRI 1
//#ExpectedStdout: inline read a
//#ExpectedStdout: inline atmark 1
//#ExpectedStdout: inline read Vb
//#ExpectedStdout: inline oob EINVAL
//#ExpectedExitCode: 0
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

/// Waits up to five seconds for `sig`; its siginfo goes to `si`.
static int took(int sig, siginfo_t *si)
{
	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, sig);
	struct timespec five = { 5, 0 };
	return sigtimedwait(&set, si, &five) == sig;
}

/// A connected pair on loopback: the connecting socket and the accepted one.
static void tcp_pair(int l, struct sockaddr_in *addr, int *c, int *s)
{
	*c = socket(AF_INET, SOCK_STREAM, 0);
	if (connect(*c, (struct sockaddr *)addr, sizeof *addr))
		perror("connect");
	*s = accept(l, 0, 0);
}

static int pollpri(int fd, int ms)
{
	struct pollfd p = { fd, POLLPRI, 0 };
	return poll(&p, 1, ms) == 1 && (p.revents & POLLPRI);
}

static void show_read(const char *what, int fd)
{
	char buf[16];
	ssize_t n = read(fd, buf, sizeof buf);
	printf("%s %.*s\n", what, (int)(n > 0 ? n : 0), buf);
}

static int atmark(int fd)
{
	int at = -1;
	ioctl(fd, SIOCATMARK, &at);
	return at;
}

static const char *oob_errno(int fd)
{
	char b;
	if (recv(fd, &b, 1, MSG_OOB) >= 0)
		return "none";
	return errno == EINVAL ? "EINVAL" : strerror(errno);
}

int main(void)
{
	int rt = SIGRTMIN;
	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, SIGURG);
	sigaddset(&set, rt);
	sigprocmask(SIG_BLOCK, &set, 0);

	int l = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in addr = { .sin_family = AF_INET };
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	socklen_t len = sizeof addr;
	if (bind(l, (struct sockaddr *)&addr, sizeof addr) || listen(l, 4) ||
	    getsockname(l, (struct sockaddr *)&addr, &len))
		return 1;

	int c, s;
	tcp_pair(l, &addr, &c, &s);
	fcntl(s, F_SETOWN, getpid());
	send(c, "ab", 2, 0);
	send(c, "U", 1, MSG_OOB);
	send(c, "cd", 2, 0);
	siginfo_t si;
	printf("sigurg %d\n", took(SIGURG, &si));
	printf("pollpri %d\n", pollpri(s, 5000));
	int inq = -1;
	ioctl(s, FIONREAD, &inq);
	printf("inq %d\n", inq);
	show_read("read", s);
	printf("atmark %d\n", atmark(s));
	char b = 0;
	recv(s, &b, 1, MSG_OOB);
	printf("oob %c\n", b);
	printf("oob again %s\n", oob_errno(s));
	printf("pollpri %d\n", pollpri(s, 0));
	show_read("read", s);

	fcntl(s, F_SETSIG, rt);
	fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_ASYNC);
	send(c, "W", 1, MSG_OOB);
	took(SIGURG, &si);
	int got = took(rt, &si);
	printf("setsig %s %d\n", got && si.si_code == POLL_PRI ? "POLL_PRI" : "?",
	       got && si.si_fd == s);

	int c2, s2;
	tcp_pair(l, &addr, &c2, &s2);
	int on = 1;
	setsockopt(s2, SOL_SOCKET, SO_OOBINLINE, &on, sizeof on);
	fcntl(s2, F_SETOWN, getpid());
	send(c2, "a", 1, 0);
	send(c2, "V", 1, MSG_OOB);
	send(c2, "b", 1, 0);
	took(SIGURG, &si);
	for (int i = 0; i < 500 && (ioctl(s2, FIONREAD, &inq), inq < 3); i++)
		usleep(10000);
	show_read("inline read", s2);
	printf("inline atmark %d\n", atmark(s2));
	show_read("inline read", s2);
	printf("inline oob %s\n", oob_errno(s2));
	return 0;
}
