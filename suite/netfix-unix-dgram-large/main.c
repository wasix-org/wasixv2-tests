// Unix datagram and seqpacket records larger than 64 KiB arrive whole, up to SO_SNDBUF less
// the skb overhead (sk_sndbuf - 32), which a larger SO_SNDBUF raises; past it, EMSGSIZE. A short
// buffer truncates with MSG_TRUNC and the real length.
//#ExpectedStdout: ok
#include <errno.h>
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

#define BIG (400 * 1024)

static void records(int type, unsigned char *data, unsigned char *buf)
{
	int sv[2];
	CHECK(socketpair(AF_UNIX, type, 0, sv) == 0);
	int sndbuf = 0;
	socklen_t len = sizeof sndbuf;
	CHECK(getsockopt(sv[0], SOL_SOCKET, SO_SNDBUF, &sndbuf, &len) == 0 && sndbuf > 0);

	CHECK(send(sv[0], data, 100000, 0) == 100000);
	CHECK(recv(sv[1], buf, BIG, 0) == 100000);
	CHECK(memcmp(buf, data, 100000) == 0);

	// The largest record the default buffer takes, and one byte more.
	size_t max = (size_t)sndbuf - 32;
	CHECK(send(sv[0], data, max, 0) == (ssize_t)max);
	CHECK(recv(sv[1], buf, BIG, 0) == (ssize_t)max);
	CHECK(memcmp(buf, data, max) == 0);
	CHECK(send(sv[0], data, max + 1, 0) == -1 && errno == EMSGSIZE);

	// Truncation reports the record's length with MSG_TRUNC.
	CHECK(send(sv[0], data, 70000, 0) == 70000);
	CHECK(recv(sv[1], buf, 1000, MSG_TRUNC) == 70000);
	CHECK(memcmp(buf, data, 1000) == 0);

	// A larger SO_SNDBUF takes larger records (Linux doubles the value, up to wmem_max).
	int want = 200 * 1024;
	CHECK(setsockopt(sv[0], SOL_SOCKET, SO_SNDBUF, &want, sizeof want) == 0);
	len = sizeof sndbuf;
	CHECK(getsockopt(sv[0], SOL_SOCKET, SO_SNDBUF, &sndbuf, &len) == 0);
	CHECK(sndbuf == 2 * want);
	CHECK(send(sv[0], data, 300000, 0) == 300000);
	CHECK(recv(sv[1], buf, BIG, 0) == 300000);
	CHECK(memcmp(buf, data, 300000) == 0);
	close(sv[0]);
	close(sv[1]);
}

int main(void)
{
	unsigned char *data = malloc(BIG), *buf = malloc(BIG);
	CHECK(data != NULL && buf != NULL);
	for (int i = 0; i < BIG; i++)
		data[i] = (unsigned char)(i * 13 + i / 1000);
	records(SOCK_DGRAM, data, buf);
	records(SOCK_SEQPACKET, data, buf);
	puts("ok");
	return 0;
}
