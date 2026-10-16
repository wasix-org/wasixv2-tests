// accept at RLIMIT_NOFILE fails with EMFILE and leaves the connection queued, so a server that
// sheds load and retries gets it, for TCP and Unix sockets. A Unix accept whose address copy
// fails closes the new socket, and its client sees EOF instead of hanging.
//#ExpectedStdout: ok
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
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

/// Lowers RLIMIT_NOFILE so that no descriptor is free, accepts, then restores it.
static void accept_at_limit(int listener, int client)
{
	struct rlimit old;
	CHECK(getrlimit(RLIMIT_NOFILE, &old) == 0);
	int spare = dup(0);
	CHECK(spare >= 0);
	close(spare);
	// Every descriptor below `spare` is taken.
	struct rlimit tight = { .rlim_cur = spare, .rlim_max = old.rlim_max };
	CHECK(setrlimit(RLIMIT_NOFILE, &tight) == 0);
	CHECK(accept(listener, NULL, NULL) == -1 && errno == EMFILE);
	CHECK(accept(listener, NULL, NULL) == -1 && errno == EMFILE);
	CHECK(setrlimit(RLIMIT_NOFILE, &old) == 0);

	int server = accept(listener, NULL, NULL);
	CHECK(server >= 0);
	CHECK(write(client, "ping", 4) == 4);
	char buf[8];
	CHECK(read(server, buf, sizeof buf) == 4 && memcmp(buf, "ping", 4) == 0);
	CHECK(write(server, "pong", 4) == 4);
	CHECK(read(client, buf, sizeof buf) == 4 && memcmp(buf, "pong", 4) == 0);
	close(server);
}

int main(void)
{
	int tl = socket(AF_INET, SOCK_STREAM, 0);
	CHECK(tl >= 0);
	struct sockaddr_in addr = { .sin_family = AF_INET };
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	CHECK(bind(tl, (struct sockaddr *)&addr, sizeof addr) == 0);
	socklen_t len = sizeof addr;
	CHECK(getsockname(tl, (struct sockaddr *)&addr, &len) == 0);
	CHECK(listen(tl, 4) == 0);
	int tc = socket(AF_INET, SOCK_STREAM, 0);
	CHECK(connect(tc, (struct sockaddr *)&addr, sizeof addr) == 0);
	accept_at_limit(tl, tc);

	int ul = socket(AF_UNIX, SOCK_STREAM, 0);
	CHECK(ul >= 0);
	struct sockaddr_un un = { .sun_family = AF_UNIX };
	memcpy(un.sun_path, "\0emfile", 7);
	socklen_t ulen = offsetof(struct sockaddr_un, sun_path) + 7;
	CHECK(bind(ul, (struct sockaddr *)&un, ulen) == 0);
	CHECK(listen(ul, 4) == 0);
	int uc = socket(AF_UNIX, SOCK_STREAM, 0);
	CHECK(connect(uc, (struct sockaddr *)&un, ulen) == 0);
	accept_at_limit(ul, uc);

	// A negative address length is EINVAL after the connection was taken; the accepted
	// socket is closed again, so the client reads EOF.
	int uc2 = socket(AF_UNIX, SOCK_STREAM, 0);
	CHECK(connect(uc2, (struct sockaddr *)&un, ulen) == 0);
	struct sockaddr_un peer;
	socklen_t bad = (socklen_t)-1;
	CHECK(accept(ul, (struct sockaddr *)&peer, &bad) == -1 && errno == EINVAL);
	char buf[4];
	CHECK(read(uc2, buf, sizeof buf) == 0);
	puts("ok");
	return 0;
}
