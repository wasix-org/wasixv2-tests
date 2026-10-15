// select and pselect over pipes and sockets: ready sets are rewritten in place, timeouts
// expire, a closed descriptor is EBADF, and nfds bounds the scan.
//#ExpectedStdout: ok
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
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
	int p[2], sv[2];
	CHECK(pipe(p) == 0);
	CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
	int maxfd = p[0];
	if (p[1] > maxfd)
		maxfd = p[1];
	if (sv[0] > maxfd)
		maxfd = sv[0];
	if (sv[1] > maxfd)
		maxfd = sv[1];

	fd_set r, w;
	FD_ZERO(&r);
	FD_ZERO(&w);
	FD_SET(p[0], &r);
	FD_SET(sv[1], &r);
	FD_SET(p[1], &w);
	struct timeval tv = { 0, 0 };
	CHECK(select(maxfd + 1, &r, &w, NULL, &tv) == 1);
	CHECK(!FD_ISSET(p[0], &r) && !FD_ISSET(sv[1], &r) && FD_ISSET(p[1], &w));

	CHECK(write(sv[0], "s", 1) == 1);
	CHECK(write(p[1], "p", 1) == 1);
	FD_ZERO(&r);
	FD_SET(p[0], &r);
	FD_SET(sv[1], &r);
	tv.tv_sec = 5;
	tv.tv_usec = 0;
	CHECK(select(maxfd + 1, &r, NULL, NULL, &tv) == 2);
	CHECK(FD_ISSET(p[0], &r) && FD_ISSET(sv[1], &r));

	// Timeout. (Linux writes the time left back, but musl's select does not pass it on, so
	// the value of tv afterwards is not checked.)
	char c;
	CHECK(read(p[0], &c, 1) == 1);
	CHECK(read(sv[1], &c, 1) == 1);
	FD_ZERO(&r);
	FD_SET(p[0], &r);
	tv.tv_sec = 0;
	tv.tv_usec = 30000;
	CHECK(select(p[0] + 1, &r, NULL, NULL, &tv) == 0);
	CHECK(!FD_ISSET(p[0], &r));

	// Descriptors at or above nfds are ignored.
	CHECK(write(p[1], "p", 1) == 1);
	FD_ZERO(&r);
	FD_SET(p[0], &r);
	tv.tv_sec = 0;
	tv.tv_usec = 0;
	CHECK(select(p[0], &r, NULL, NULL, &tv) == 0);

	// pselect with a timespec.
	FD_ZERO(&r);
	FD_SET(p[0], &r);
	struct timespec ts = { 1, 0 };
	CHECK(pselect(p[0] + 1, &r, NULL, NULL, &ts, NULL) == 1 && FD_ISSET(p[0], &r));

	// A closed descriptor in a set: EBADF.
	int q[2];
	CHECK(pipe(q) == 0);
	close(q[0]);
	FD_ZERO(&r);
	FD_SET(q[0], &r);
	tv.tv_sec = 0;
	CHECK(select(q[0] + 1, &r, NULL, NULL, &tv) == -1 && errno == EBADF);

	// Exceptional conditions on a pipe: none.
	fd_set e;
	FD_ZERO(&e);
	FD_SET(p[0], &e);
	tv.tv_sec = 0;
	CHECK(select(p[0] + 1, NULL, NULL, &e, &tv) == 0);

	CHECK(select(-1, NULL, NULL, NULL, &tv) == -1 && errno == EINVAL);
	printf("ok\n");
	return 0;
}
