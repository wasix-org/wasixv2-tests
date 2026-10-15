// Non-blocking TCP connect over loopback: connect returns at once (EINPROGRESS or success),
// the socket becomes writable with SO_ERROR 0, and a second connect reports EISCONN. A refused
// connect reports ECONNREFUSED, at once or through SO_ERROR.
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

static int so_error(int fd)
{
	int err = -1;
	socklen_t len = sizeof err;
	CHECK(getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0);
	return err;
}

int main(void)
{
	int srv = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in addr = { .sin_family = AF_INET };
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	CHECK(bind(srv, (struct sockaddr *)&addr, sizeof addr) == 0);
	socklen_t len = sizeof addr;
	CHECK(getsockname(srv, (struct sockaddr *)&addr, &len) == 0);
	CHECK(listen(srv, 1) == 0);

	int cli = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	CHECK(cli >= 0);
	int r = connect(cli, (struct sockaddr *)&addr, sizeof addr);
	CHECK(r == 0 || errno == EINPROGRESS);
	struct pollfd pfd = { .fd = cli, .events = POLLOUT };
	CHECK(poll(&pfd, 1, 1000) == 1 && (pfd.revents & POLLOUT));
	CHECK(so_error(cli) == 0);
	// After EINPROGRESS, the first connect once established reports success; then EISCONN.
	if (r != 0)
		CHECK(connect(cli, (struct sockaddr *)&addr, sizeof addr) == 0);
	CHECK(connect(cli, (struct sockaddr *)&addr, sizeof addr) == -1 && errno == EISCONN);

	int conn = accept(srv, NULL, NULL);
	CHECK(conn >= 0);
	CHECK(write(conn, "z", 1) == 1);
	char c;
	pfd.events = POLLIN;
	CHECK(poll(&pfd, 1, 1000) == 1);
	CHECK(read(cli, &c, 1) == 1 && c == 'z');
	close(conn);
	close(cli);

	// Nobody listens on the port once the listener is closed.
	close(srv);
	int bad = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	r = connect(bad, (struct sockaddr *)&addr, sizeof addr);
	if (r == -1 && errno == EINPROGRESS) {
		pfd.fd = bad;
		pfd.events = POLLOUT;
		CHECK(poll(&pfd, 1, 1000) == 1);
		CHECK(pfd.revents & (POLLERR | POLLHUP));
		CHECK(so_error(bad) == ECONNREFUSED);
		// SO_ERROR is cleared by reading it.
		CHECK(so_error(bad) == 0);
	} else {
		CHECK(r == -1 && errno == ECONNREFUSED);
	}
	close(bad);
	printf("ok\n");
	return 0;
}
