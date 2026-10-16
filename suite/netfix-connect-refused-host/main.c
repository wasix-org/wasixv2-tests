// A TCP connect to a closed port on the host fails as on Linux: the non-blocking attempt polls
// ERR|HUP, SO_ERROR says ECONNREFUSED, the socket has no peer and no port, the next connect
// reports the failure (ECONNABORTED once SO_ERROR took the error) and the one after retries. A
// blocking connect fails with ECONNREFUSED, as often as it is tried.
//#ExpectedStdout: ok
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
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
	// The gateway reaches the host's loopback, where nothing listens on port 1.
	struct sockaddr_in host = { .sin_family = AF_INET, .sin_port = htons(1) };
	CHECK(inet_pton(AF_INET, "10.0.2.2", &host.sin_addr) == 1);

	int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	CHECK(fd >= 0);
	CHECK(connect(fd, (struct sockaddr *)&host, sizeof host) == -1 && errno == EINPROGRESS);
	struct sockaddr_in local;
	socklen_t len = sizeof local;
	CHECK(getsockname(fd, (struct sockaddr *)&local, &len) == 0 && local.sin_port != 0);

	struct pollfd p = { .fd = fd, .events = POLLOUT };
	CHECK(poll(&p, 1, 10000) == 1);
	CHECK((p.revents & (POLLERR | POLLHUP)) == (POLLERR | POLLHUP));

	// tcp_done: no peer, state CLOSE. The port went back, but getsockname still shows it.
	in_port_t port = local.sin_port;
	struct sockaddr_in peer;
	len = sizeof peer;
	CHECK(getpeername(fd, (struct sockaddr *)&peer, &len) == -1 && errno == ENOTCONN);
	struct tcp_info info;
	len = sizeof info;
	CHECK(getsockopt(fd, IPPROTO_TCP, TCP_INFO, &info, &len) == 0);
	CHECK(info.tcpi_state == TCP_CLOSE);
	len = sizeof local;
	CHECK(getsockname(fd, (struct sockaddr *)&local, &len) == 0 && local.sin_port == port);

	int err = 0;
	len = sizeof err;
	CHECK(getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == ECONNREFUSED);
	CHECK(connect(fd, (struct sockaddr *)&host, sizeof host) == -1 && errno == ECONNABORTED);
	CHECK(connect(fd, (struct sockaddr *)&host, sizeof host) == -1 && errno == EINPROGRESS);
	CHECK(poll(&p, 1, 10000) == 1 && (p.revents & POLLERR));
	close(fd);

	int b = socket(AF_INET, SOCK_STREAM, 0);
	CHECK(b >= 0);
	CHECK(connect(b, (struct sockaddr *)&host, sizeof host) == -1 && errno == ECONNREFUSED);
	CHECK(connect(b, (struct sockaddr *)&host, sizeof host) == -1 && errno == ECONNREFUSED);
	len = sizeof peer;
	CHECK(getpeername(b, (struct sockaddr *)&peer, &len) == -1 && errno == ENOTCONN);
	puts("ok");
	return 0;
}
