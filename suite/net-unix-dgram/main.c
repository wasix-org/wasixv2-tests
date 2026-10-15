// AF_UNIX datagram sockets in the abstract namespace: sendto a name, recvfrom reports the
// sender's name, connected sockets, and errors for unknown names.
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
	struct sockaddr_un an, bn, nobody;
	socklen_t alen = abstract(&an, "wasixv2-dgram-a");
	socklen_t blen = abstract(&bn, "wasixv2-dgram-b");
	socklen_t nlen = abstract(&nobody, "wasixv2-dgram-nobody");

	int a = socket(AF_UNIX, SOCK_DGRAM, 0);
	int b = socket(AF_UNIX, SOCK_DGRAM, 0);
	CHECK(a >= 0 && b >= 0);
	CHECK(bind(a, (struct sockaddr *)&an, alen) == 0);
	CHECK(bind(b, (struct sockaddr *)&bn, blen) == 0);

	CHECK(sendto(a, "to-b", 4, 0, (struct sockaddr *)&bn, blen) == 4);
	char buf[32];
	struct sockaddr_un from;
	socklen_t flen = sizeof from;
	CHECK(recvfrom(b, buf, sizeof buf, 0, (struct sockaddr *)&from, &flen) == 4);
	CHECK(memcmp(buf, "to-b", 4) == 0);
	CHECK(flen == alen && memcmp(&from, &an, alen) == 0);

	CHECK(sendto(a, "x", 1, 0, (struct sockaddr *)&nobody, nlen) == -1 &&
	      errno == ECONNREFUSED);

	// Unconnected and no address: not connected.
	CHECK(send(a, "x", 1, 0) == -1 && errno == ENOTCONN);

	CHECK(connect(a, (struct sockaddr *)&bn, blen) == 0);
	CHECK(send(a, "conn", 4, 0) == 4);
	flen = sizeof from;
	CHECK(recvfrom(b, buf, sizeof buf, 0, (struct sockaddr *)&from, &flen) == 4);
	CHECK(memcmp(buf, "conn", 4) == 0);

	// A connected socket only receives from its peer.
	int c = socket(AF_UNIX, SOCK_DGRAM, 0);
	CHECK(c >= 0);
	CHECK(sendto(c, "x", 1, 0, (struct sockaddr *)&an, alen) == -1 && errno == EPERM);
	CHECK(sendto(b, "from-b", 6, 0, (struct sockaddr *)&an, alen) == 6);
	CHECK(recv(a, buf, sizeof buf, 0) == 6 && memcmp(buf, "from-b", 6) == 0);

	// An unbound sender is unnamed: recvfrom reports an empty address.
	CHECK(sendto(c, "anon", 4, 0, (struct sockaddr *)&bn, blen) == 4);
	flen = sizeof from;
	CHECK(recvfrom(b, buf, sizeof buf, 0, (struct sockaddr *)&from, &flen) == 4);
	CHECK(flen == 0);

	close(a);
	close(b);
	close(c);
	printf("ok\n");
	return 0;
}
