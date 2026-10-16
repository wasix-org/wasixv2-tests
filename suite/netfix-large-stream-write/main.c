// A blocking send of megabytes on a stream socket transfers all of it in one call, as Linux
// does, over Unix and TCP loopback sockets, with send and with sendmsg over several iovecs; the
// reader gets it whole with one MSG_WAITALL receive.
//#ExpectedStdout: ok
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

#define CHECK(c)                                                                     \
	do {                                                                         \
		if (!(c)) {                                                          \
			printf("FAIL %s:%d: %s (errno=%d %s)\n", __FILE__, __LINE__, \
			       #c, errno, strerror(errno));                          \
			exit(1);                                                     \
		}                                                                    \
	} while (0)

#define SIZE (4 << 20)

struct reader {
	int fd;
	unsigned char *buf;
	ssize_t got;
};

static void *read_all(void *arg)
{
	struct reader *r = arg;
	r->got = recv(r->fd, r->buf, SIZE, MSG_WAITALL);
	return NULL;
}

static void transfer(int tx, int rx, const unsigned char *data, int use_sendmsg)
{
	struct reader r = { .fd = rx, .buf = malloc(SIZE) };
	CHECK(r.buf != NULL);
	pthread_t t;
	CHECK(pthread_create(&t, NULL, read_all, &r) == 0);
	ssize_t sent;
	if (use_sendmsg) {
		struct iovec iov[3] = {
			{ .iov_base = (void *)data, .iov_len = 1000 },
			{ .iov_base = (void *)(data + 1000), .iov_len = SIZE / 2 },
			{ .iov_base = (void *)(data + 1000 + SIZE / 2), .iov_len = SIZE / 2 - 1000 },
		};
		struct msghdr msg = { .msg_iov = iov, .msg_iovlen = 3 };
		sent = sendmsg(tx, &msg, 0);
	} else {
		sent = send(tx, data, SIZE, 0);
	}
	CHECK(sent == SIZE);
	CHECK(pthread_join(t, NULL) == 0);
	CHECK(r.got == SIZE);
	CHECK(memcmp(r.buf, data, SIZE) == 0);
	free(r.buf);
}

int main(void)
{
	unsigned char *data = malloc(SIZE);
	CHECK(data != NULL);
	for (int i = 0; i < SIZE; i++)
		data[i] = (unsigned char)(i * 7 + i / 4096);

	int sv[2];
	CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
	transfer(sv[0], sv[1], data, 0);
	transfer(sv[0], sv[1], data, 1);

	int l = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in addr = { .sin_family = AF_INET };
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	CHECK(bind(l, (struct sockaddr *)&addr, sizeof addr) == 0);
	socklen_t len = sizeof addr;
	CHECK(getsockname(l, (struct sockaddr *)&addr, &len) == 0);
	CHECK(listen(l, 1) == 0);
	int c = socket(AF_INET, SOCK_STREAM, 0);
	CHECK(connect(c, (struct sockaddr *)&addr, sizeof addr) == 0);
	int s = accept(l, NULL, NULL);
	CHECK(s >= 0);
	transfer(c, s, data, 0);
	transfer(s, c, data, 1);
	puts("ok");
	return 0;
}
