// send, sendto, sendmsg and sendmmsg on a stream socket whose peer is gone raise SIGPIPE with
// EPIPE, unless MSG_NOSIGNAL; datagram sockets fail with EPIPE after shutdown without the
// signal.
//#ExpectedStdout: ok
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

#define CHECK(c)                                                                     \
	do {                                                                         \
		if (!(c)) {                                                          \
			printf("FAIL %s:%d: %s (errno=%d %s)\n", __FILE__, __LINE__, \
			       #c, errno, strerror(errno));                          \
			exit(1);                                                     \
		}                                                                    \
	} while (0)

static volatile sig_atomic_t pipes;

static void on_pipe(int sig)
{
	(void)sig;
	pipes++;
}

/// Each way of sending to `fd`, which must fail with EPIPE: one SIGPIPE each, none with
/// MSG_NOSIGNAL.
static void expect_sigpipe(int fd)
{
	char byte = 'x';
	struct iovec iov = { .iov_base = &byte, .iov_len = 1 };
	struct msghdr msg = { .msg_iov = &iov, .msg_iovlen = 1 };
	struct mmsghdr mmsg = { .msg_hdr = msg };

	pipes = 0;
	CHECK(send(fd, &byte, 1, 0) == -1 && errno == EPIPE);
	CHECK(pipes == 1);
	CHECK(sendto(fd, &byte, 1, 0, NULL, 0) == -1 && errno == EPIPE);
	CHECK(pipes == 2);
	CHECK(sendmsg(fd, &msg, 0) == -1 && errno == EPIPE);
	CHECK(pipes == 3);
	CHECK(sendmmsg(fd, &mmsg, 1, 0) == -1 && errno == EPIPE);
	CHECK(pipes == 4);
	CHECK(send(fd, &byte, 1, MSG_NOSIGNAL) == -1 && errno == EPIPE);
	CHECK(sendmsg(fd, &msg, MSG_NOSIGNAL) == -1 && errno == EPIPE);
	CHECK(pipes == 4);
}

int main(void)
{
	struct sigaction sa = { .sa_handler = on_pipe };
	CHECK(sigaction(SIGPIPE, &sa, NULL) == 0);

	int sv[2];
	CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
	close(sv[1]);
	expect_sigpipe(sv[0]);

	// TCP: the peer closed; the first write meets its RST, every later one is EPIPE.
	int l = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in addr = { .sin_family = AF_INET };
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	CHECK(bind(l, (struct sockaddr *)&addr, sizeof addr) == 0);
	socklen_t len = sizeof addr;
	CHECK(getsockname(l, (struct sockaddr *)&addr, &len) == 0);
	CHECK(listen(l, 1) == 0);
	int c = socket(AF_INET, SOCK_STREAM, 0);
	CHECK(connect(c, (struct sockaddr *)&addr, sizeof addr) == 0);
	int s = accept(l, NULL, NULL);
	CHECK(s >= 0);
	close(s);
	CHECK(shutdown(c, SHUT_WR) == 0);
	expect_sigpipe(c);

	// Datagram sockets after shutdown: EPIPE, never SIGPIPE.
	pipes = 0;
	int d[2];
	CHECK(socketpair(AF_UNIX, SOCK_DGRAM, 0, d) == 0);
	CHECK(shutdown(d[0], SHUT_WR) == 0);
	CHECK(send(d[0], "x", 1, 0) == -1 && errno == EPIPE);
	int u = socket(AF_INET, SOCK_DGRAM, 0);
	CHECK(connect(u, (struct sockaddr *)&addr, sizeof addr) == 0);
	CHECK(shutdown(u, SHUT_WR) == 0);
	CHECK(send(u, "x", 1, 0) == -1 && errno == EPIPE);
	CHECK(pipes == 0);
	puts("ok");
	return 0;
}
