// epoll over TCP loopback sockets: a listener becomes readable when a client connects, a
// connected socket is writable, readable on data, and reports EPOLLRDHUP after the peer's FIN.
//#ExpectedStdout: ok
#define _GNU_SOURCE
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
	int srv = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	CHECK(srv >= 0);
	struct sockaddr_in addr = { .sin_family = AF_INET };
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	CHECK(bind(srv, (struct sockaddr *)&addr, sizeof addr) == 0);
	socklen_t len = sizeof addr;
	CHECK(getsockname(srv, (struct sockaddr *)&addr, &len) == 0);
	CHECK(listen(srv, 8) == 0);

	int ep = epoll_create1(0);
	struct epoll_event ev = { .events = EPOLLIN, .data.fd = srv };
	CHECK(epoll_ctl(ep, EPOLL_CTL_ADD, srv, &ev) == 0);
	struct epoll_event out[4];
	CHECK(epoll_wait(ep, out, 4, 0) == 0);

	int cli = socket(AF_INET, SOCK_STREAM, 0);
	CHECK(connect(cli, (struct sockaddr *)&addr, sizeof addr) == 0);
	CHECK(epoll_wait(ep, out, 4, 1000) == 1 && out[0].data.fd == srv &&
	      out[0].events == EPOLLIN);
	int conn = accept4(srv, NULL, NULL, SOCK_NONBLOCK);
	CHECK(conn >= 0);
	CHECK(epoll_wait(ep, out, 4, 0) == 0);

	ev.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLET;
	ev.data.fd = conn;
	CHECK(epoll_ctl(ep, EPOLL_CTL_ADD, conn, &ev) == 0);
	CHECK(epoll_wait(ep, out, 4, 0) == 1 && out[0].data.fd == conn &&
	      out[0].events == EPOLLOUT);

	CHECK(write(cli, "data", 4) == 4);
	CHECK(epoll_wait(ep, out, 4, 1000) == 1 && (out[0].events & EPOLLIN));
	char buf[16];
	CHECK(read(conn, buf, sizeof buf) == 4);
	CHECK(read(conn, buf, sizeof buf) == -1 && errno == EAGAIN);

	CHECK(shutdown(cli, SHUT_WR) == 0);
	CHECK(epoll_wait(ep, out, 4, 1000) == 1);
	CHECK(out[0].events == (EPOLLIN | EPOLLOUT | EPOLLRDHUP));
	CHECK(read(conn, buf, sizeof buf) == 0);

	// Both directions shut: EPOLLHUP too.
	CHECK(shutdown(conn, SHUT_WR) == 0);
	CHECK(epoll_wait(ep, out, 4, 1000) == 1);
	CHECK(out[0].events == (EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLHUP));

	close(conn);
	close(cli);
	close(srv);
	printf("ok\n");
	return 0;
}
