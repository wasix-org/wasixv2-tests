// A UDP socket watched by epoll before its first datagram to a host address, as event loops
// (libuv, c-ares) do: what the host sends back (here the ICMP port unreachable a connected
// socket learns as ECONNREFUSED) must wake the epoll registered earlier.
//#ExpectedStdout: ok
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
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
	int fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
	CHECK(fd >= 0);
	int ep = epoll_create1(0);
	CHECK(ep >= 0);
	struct epoll_event ev = { .events = EPOLLIN, .data.fd = fd };
	CHECK(epoll_ctl(ep, EPOLL_CTL_ADD, fd, &ev) == 0);
	struct epoll_event out[2];
	CHECK(epoll_wait(ep, out, 2, 0) == 0);

	// The gateway reaches the host's loopback, where nothing listens on port 1.
	struct sockaddr_in host = { .sin_family = AF_INET, .sin_port = htons(1) };
	CHECK(inet_pton(AF_INET, "10.0.2.2", &host.sin_addr) == 1);
	CHECK(connect(fd, (struct sockaddr *)&host, sizeof host) == 0);
	CHECK(send(fd, "query", 5, 0) == 5);

	// Woken by the host's answer, on the registration made before the socket left the
	// namespace.
	int n = epoll_wait(ep, out, 2, 10000);
	CHECK(n == 1);
	CHECK(out[0].data.fd == fd && (out[0].events & EPOLLERR));
	char buf[16];
	CHECK(recv(fd, buf, sizeof buf, 0) == -1 && errno == ECONNREFUSED);
	puts("ok");
	return 0;
}
