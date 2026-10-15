// AF_UNIX stream sockets in the abstract namespace: bind, listen, connect, accept, the names
// each end reports, and the errors for taken and unknown names.
//#ExpectedStdout: ok
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define CHECK(c)                                                                     \
	do {                                                                         \
		if (!(c)) {                                                          \
			printf("FAIL %s:%d: %s (errno=%d %s)\n", __FILE__, __LINE__, \
			       #c, errno, strerror(errno));                          \
			exit(1);                                                     \
		}                                                                    \
	} while (0)

static socklen_t abstract(struct sockaddr_un *sun, const char *name)
{
	memset(sun, 0, sizeof *sun);
	sun->sun_family = AF_UNIX;
	size_t n = strlen(name);
	memcpy(sun->sun_path + 1, name, n);
	return offsetof(struct sockaddr_un, sun_path) + 1 + n;
}

int main(void)
{
	struct sockaddr_un addr;
	socklen_t alen = abstract(&addr, "wasixv2-net-unix-stream");

	int srv = socket(AF_UNIX, SOCK_STREAM, 0);
	CHECK(srv >= 0);
	CHECK(bind(srv, (struct sockaddr *)&addr, alen) == 0);

	struct sockaddr_un got;
	socklen_t len = sizeof got;
	CHECK(getsockname(srv, (struct sockaddr *)&got, &len) == 0);
	CHECK(len == alen && memcmp(&got, &addr, alen) == 0);

	// The name is taken.
	int dup = socket(AF_UNIX, SOCK_STREAM, 0);
	CHECK(dup >= 0);
	CHECK(bind(dup, (struct sockaddr *)&addr, alen) == -1 && errno == EADDRINUSE);

	// Bound but not listening: refused.
	int c0 = socket(AF_UNIX, SOCK_STREAM, 0);
	CHECK(connect(c0, (struct sockaddr *)&addr, alen) == -1 && errno == ECONNREFUSED);
	close(c0);

	CHECK(listen(srv, 2) == 0);

	int cli = socket(AF_UNIX, SOCK_STREAM, 0);
	CHECK(cli >= 0);
	CHECK(connect(cli, (struct sockaddr *)&addr, alen) == 0);
	// Connecting again: already connected.
	CHECK(connect(cli, (struct sockaddr *)&addr, alen) == -1 && errno == EISCONN);

	len = sizeof got;
	CHECK(getpeername(cli, (struct sockaddr *)&got, &len) == 0);
	CHECK(len == alen && memcmp(&got, &addr, alen) == 0);

	// The client was never bound, so the accepted socket's peer is unnamed.
	len = sizeof got;
	int conn = accept(srv, (struct sockaddr *)&got, &len);
	CHECK(conn >= 0);
	CHECK(len == sizeof(sa_family_t) && got.sun_family == AF_UNIX);
	// The accepted socket is named like its listener.
	len = sizeof got;
	CHECK(getsockname(conn, (struct sockaddr *)&got, &len) == 0);
	CHECK(len == alen && memcmp(&got, &addr, alen) == 0);

	CHECK(write(cli, "hello", 5) == 5);
	char buf[16];
	CHECK(read(conn, buf, sizeof buf) == 5 && memcmp(buf, "hello", 5) == 0);
	CHECK(write(conn, "world", 5) == 5);
	CHECK(read(cli, buf, sizeof buf) == 5 && memcmp(buf, "world", 5) == 0);
	close(conn);
	CHECK(read(cli, buf, sizeof buf) == 0);
	close(cli);

	// An unknown name: refused.
	struct sockaddr_un other;
	socklen_t olen = abstract(&other, "wasixv2-net-unix-stream-nobody");
	int c1 = socket(AF_UNIX, SOCK_STREAM, 0);
	CHECK(connect(c1, (struct sockaddr *)&other, olen) == -1 && errno == ECONNREFUSED);
	close(c1);

	// Autobind: binding only the family picks an abstract name of five hex digits.
	int ab = socket(AF_UNIX, SOCK_STREAM, 0);
	struct sockaddr_un fam = { .sun_family = AF_UNIX };
	CHECK(bind(ab, (struct sockaddr *)&fam, sizeof(sa_family_t)) == 0);
	len = sizeof got;
	CHECK(getsockname(ab, (struct sockaddr *)&got, &len) == 0);
	CHECK(len == offsetof(struct sockaddr_un, sun_path) + 6 && got.sun_path[0] == 0);
	close(ab);

	// Closing the listener frees the name.
	close(srv);
	CHECK(bind(dup, (struct sockaddr *)&addr, alen) == 0);
	close(dup);
	printf("ok\n");
	return 0;
}
