// AF_UNIX stream sockets bound to a filesystem path: bind creates a socket inode, a second bind
// is EADDRINUSE, connect finds the listener through the path, and unlink frees the name.
//#ExpectedStdout: ok
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
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

int main(void)
{
	struct sockaddr_un addr = { .sun_family = AF_UNIX };
	snprintf(addr.sun_path, sizeof addr.sun_path, "/tmp/net-unix-path.%d", getpid());
	socklen_t alen = offsetof(struct sockaddr_un, sun_path) + strlen(addr.sun_path) + 1;
	unlink(addr.sun_path);

	int srv = socket(AF_UNIX, SOCK_STREAM, 0);
	CHECK(srv >= 0);
	CHECK(bind(srv, (struct sockaddr *)&addr, alen) == 0);
	struct stat st;
	CHECK(stat(addr.sun_path, &st) == 0 && S_ISSOCK(st.st_mode));

	struct sockaddr_un got;
	socklen_t len = sizeof got;
	CHECK(getsockname(srv, (struct sockaddr *)&got, &len) == 0);
	CHECK(len == alen && strcmp(got.sun_path, addr.sun_path) == 0);

	int dup = socket(AF_UNIX, SOCK_STREAM, 0);
	CHECK(bind(dup, (struct sockaddr *)&addr, alen) == -1 && errno == EADDRINUSE);
	close(dup);

	CHECK(listen(srv, 1) == 0);
	int cli = socket(AF_UNIX, SOCK_STREAM, 0);
	CHECK(connect(cli, (struct sockaddr *)&addr, alen) == 0);
	int conn = accept(srv, NULL, NULL);
	CHECK(conn >= 0);
	CHECK(write(cli, "path", 4) == 4);
	char buf[8];
	CHECK(read(conn, buf, sizeof buf) == 4 && memcmp(buf, "path", 4) == 0);
	close(conn);
	close(cli);

	// Opening a socket inode is ENXIO.
	CHECK(open(addr.sun_path, 0) == -1 && errno == ENXIO);

	// A missing path: ENOENT.
	struct sockaddr_un none = { .sun_family = AF_UNIX };
	strcpy(none.sun_path, "/tmp/net-unix-path.missing");
	int c2 = socket(AF_UNIX, SOCK_STREAM, 0);
	CHECK(connect(c2, (struct sockaddr *)&none, sizeof none) == -1 && errno == ENOENT);
	close(c2);

	close(srv);
	// The inode outlives the socket: connecting to it now is refused, and only unlink frees it.
	int c3 = socket(AF_UNIX, SOCK_STREAM, 0);
	CHECK(connect(c3, (struct sockaddr *)&addr, alen) == -1 && errno == ECONNREFUSED);
	close(c3);
	CHECK(unlink(addr.sun_path) == 0);
	printf("ok\n");
	return 0;
}
