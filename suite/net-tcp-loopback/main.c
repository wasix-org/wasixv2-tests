// TCP over IPv4 loopback in one process: bind to an ephemeral port, connect (which completes
// before accept, as on Linux), move data both ways, then half-close and see EOF.
#define _GNU_SOURCE
//#ExpectedStdout: ok
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
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
	int srv = socket(AF_INET, SOCK_STREAM, 0);
	CHECK(srv >= 0);
	int one = 1;
	CHECK(setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one) == 0);

	struct sockaddr_in addr = { .sin_family = AF_INET };
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	CHECK(bind(srv, (struct sockaddr *)&addr, sizeof addr) == 0);
	// A second bind on a bound socket is EINVAL.
	CHECK(bind(srv, (struct sockaddr *)&addr, sizeof addr) == -1 && errno == EINVAL);

	socklen_t len = sizeof addr;
	CHECK(getsockname(srv, (struct sockaddr *)&addr, &len) == 0);
	CHECK(len == sizeof addr && addr.sin_family == AF_INET);
	CHECK(ntohs(addr.sin_port) != 0);
	CHECK(addr.sin_addr.s_addr == htonl(INADDR_LOOPBACK));

	// Not listening yet: connection refused.
	int c0 = socket(AF_INET, SOCK_STREAM, 0);
	CHECK(c0 >= 0);
	CHECK(connect(c0, (struct sockaddr *)&addr, sizeof addr) == -1 && errno == ECONNREFUSED);
	close(c0);

	CHECK(listen(srv, 4) == 0);
	int type = 0;
	len = sizeof type;
	CHECK(getsockopt(srv, SOL_SOCKET, SO_TYPE, &type, &len) == 0 && type == SOCK_STREAM);
	int acc = 0;
	len = sizeof acc;
	CHECK(getsockopt(srv, SOL_SOCKET, SO_ACCEPTCONN, &acc, &len) == 0 && acc == 1);

	int cli = socket(AF_INET, SOCK_STREAM, 0);
	CHECK(cli >= 0);
	CHECK(connect(cli, (struct sockaddr *)&addr, sizeof addr) == 0);

	struct sockaddr_in peer;
	len = sizeof peer;
	int conn = accept(srv, (struct sockaddr *)&peer, &len);
	CHECK(conn >= 0);
	CHECK(peer.sin_family == AF_INET && peer.sin_addr.s_addr == htonl(INADDR_LOOPBACK));

	// The client's local address is the accepted socket's peer address.
	struct sockaddr_in local;
	len = sizeof local;
	CHECK(getsockname(cli, (struct sockaddr *)&local, &len) == 0);
	CHECK(local.sin_port == peer.sin_port);
	struct sockaddr_in cpeer;
	len = sizeof cpeer;
	CHECK(getpeername(cli, (struct sockaddr *)&cpeer, &len) == 0);
	CHECK(cpeer.sin_port == addr.sin_port);
	// A listening socket has no peer.
	len = sizeof cpeer;
	CHECK(getpeername(srv, (struct sockaddr *)&cpeer, &len) == -1 && errno == ENOTCONN);

	CHECK(setsockopt(cli, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one) == 0);
	int nd = 0;
	len = sizeof nd;
	CHECK(getsockopt(cli, IPPROTO_TCP, TCP_NODELAY, &nd, &len) == 0 && nd != 0);

	CHECK(write(cli, "ping", 4) == 4);
	char buf[16] = { 0 };
	CHECK(read(conn, buf, sizeof buf) == 4 && memcmp(buf, "ping", 4) == 0);
	CHECK(send(conn, "pong!", 5, 0) == 5);
	memset(buf, 0, sizeof buf);
	CHECK(recv(cli, buf, 2, MSG_PEEK) == 2 && memcmp(buf, "po", 2) == 0);
	CHECK(recv(cli, buf, sizeof buf, 0) == 5 && memcmp(buf, "pong!", 5) == 0);

	// Half-close: the server reads EOF but can still send.
	CHECK(shutdown(cli, SHUT_WR) == 0);
	CHECK(read(conn, buf, sizeof buf) == 0);
	CHECK(write(conn, "bye", 3) == 3);
	CHECK(read(cli, buf, sizeof buf) == 3 && memcmp(buf, "bye", 3) == 0);
	close(conn);
	CHECK(read(cli, buf, sizeof buf) == 0);

	// Non-blocking accept with nothing queued (SOCK_NONBLOCK only applies to the new socket).
	CHECK(fcntl(srv, F_SETFL, O_NONBLOCK) == 0);
	CHECK(accept4(srv, NULL, NULL, SOCK_NONBLOCK) == -1 && errno == EAGAIN);

	close(cli);
	close(srv);
	printf("ok\n");
	return 0;
}
