// connect(AF_UNSPEC) on a TCP connection is tcp_disconnect: the peer is reset, the socket's own
// SO_ERROR becomes ECONNRESET, the autobound port goes back and getsockname shows the wildcard
// address again. A shutdown on a connection the peer reset is ENOTCONN, as for any socket in
// CLOSE.
//#ExpectedStdout: ok
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
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

static int listener(struct sockaddr_in *addr)
{
	int l = socket(AF_INET, SOCK_STREAM, 0);
	CHECK(l >= 0);
	addr->sin_family = AF_INET;
	addr->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr->sin_port = 0;
	CHECK(bind(l, (struct sockaddr *)addr, sizeof *addr) == 0);
	socklen_t len = sizeof *addr;
	CHECK(getsockname(l, (struct sockaddr *)addr, &len) == 0);
	CHECK(listen(l, 4) == 0);
	return l;
}

static int so_error(int fd)
{
	int err = -1;
	socklen_t len = sizeof err;
	CHECK(getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0);
	return err;
}

int main(void)
{
	struct sockaddr_in addr;
	int l = listener(&addr);

	int c = socket(AF_INET, SOCK_STREAM, 0);
	CHECK(c >= 0);
	CHECK(connect(c, (struct sockaddr *)&addr, sizeof addr) == 0);
	int s = accept(l, NULL, NULL);
	CHECK(s >= 0);
	struct sockaddr_in local;
	socklen_t len = sizeof local;
	CHECK(getsockname(c, (struct sockaddr *)&local, &len) == 0);
	CHECK(local.sin_addr.s_addr == htonl(INADDR_LOOPBACK) && local.sin_port != 0);

	struct sockaddr unspec = { .sa_family = AF_UNSPEC };
	CHECK(connect(c, &unspec, sizeof unspec) == 0);
	CHECK(so_error(c) == ECONNRESET);
	CHECK(so_error(c) == 0);
	len = sizeof local;
	CHECK(getsockname(c, (struct sockaddr *)&local, &len) == 0);
	CHECK(local.sin_addr.s_addr == htonl(INADDR_ANY));
	struct sockaddr_in peer;
	len = sizeof peer;
	CHECK(getpeername(c, (struct sockaddr *)&peer, &len) == -1 && errno == ENOTCONN);
	char buf[8];
	CHECK(recv(s, buf, sizeof buf, 0) == -1 && errno == ECONNRESET);
	// The socket can connect again.
	CHECK(connect(c, (struct sockaddr *)&addr, sizeof addr) == 0);
	int s2 = accept(l, NULL, NULL);
	CHECK(s2 >= 0);
	CHECK(send(c, "x", 1, 0) == 1);
	CHECK(recv(s2, buf, sizeof buf, 0) == 1);

	// Reset by the peer (closed with unread data): shutdown is ENOTCONN.
	CHECK(send(c, "unread", 6, 0) == 6);
	struct pollfd p = { .fd = s2, .events = POLLIN };
	CHECK(poll(&p, 1, 5000) == 1);
	close(s2);
	p.fd = c;
	p.events = POLLIN;
	CHECK(poll(&p, 1, 5000) == 1 && (p.revents & POLLERR));
	CHECK(shutdown(c, SHUT_WR) == -1 && errno == ENOTCONN);
	CHECK(recv(c, buf, sizeof buf, 0) == -1 && errno == ECONNRESET);

	close(c);
	close(s);
	close(l);
	puts("ok");
	return 0;
}
