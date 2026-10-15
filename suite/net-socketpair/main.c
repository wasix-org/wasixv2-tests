// socketpair(AF_UNIX) for stream, datagram and seqpacket sockets: data in both directions,
// message boundaries, EOF and EPIPE after close, and the reported addresses.
//#ExpectedStdout: stream ok
//#ExpectedStdout: dgram ok
//#ExpectedStdout: seqpacket ok
//#ExpectedStdout: ok
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
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

static void common(int type, int sv[2])
{
	CHECK(socketpair(AF_UNIX, type, 0, sv) == 0);
	int t = 0;
	socklen_t len = sizeof t;
	CHECK(getsockopt(sv[0], SOL_SOCKET, SO_TYPE, &t, &len) == 0 && t == type);
	int domain = 0;
	len = sizeof domain;
	CHECK(getsockopt(sv[0], SOL_SOCKET, SO_DOMAIN, &domain, &len) == 0 && domain == AF_UNIX);

	// An unnamed socket's address is just the family.
	struct sockaddr_un sun;
	len = sizeof sun;
	CHECK(getsockname(sv[0], (struct sockaddr *)&sun, &len) == 0);
	CHECK(len == sizeof(sa_family_t) && sun.sun_family == AF_UNIX);
	len = sizeof sun;
	CHECK(getpeername(sv[0], (struct sockaddr *)&sun, &len) == 0);
	CHECK(len == sizeof(sa_family_t));
}

static void stream(void)
{
	int sv[2];
	common(SOCK_STREAM, sv);
	CHECK(write(sv[0], "abc", 3) == 3);
	CHECK(write(sv[0], "def", 3) == 3);
	int avail = 0;
	CHECK(ioctl(sv[1], FIONREAD, &avail) == 0 && avail == 6);
	char buf[16];
	// A stream merges writes.
	CHECK(read(sv[1], buf, sizeof buf) == 6 && memcmp(buf, "abcdef", 6) == 0);
	CHECK(write(sv[1], "back", 4) == 4);
	CHECK(read(sv[0], buf, sizeof buf) == 4 && memcmp(buf, "back", 4) == 0);

	CHECK(recv(sv[0], buf, sizeof buf, MSG_DONTWAIT) == -1 && errno == EAGAIN);

	CHECK(shutdown(sv[0], SHUT_WR) == 0);
	CHECK(read(sv[1], buf, sizeof buf) == 0);
	CHECK(send(sv[0], "x", 1, MSG_NOSIGNAL) == -1 && errno == EPIPE);

	close(sv[1]);
	CHECK(read(sv[0], buf, sizeof buf) == 0);
	close(sv[0]);

	// Non-blocking flags from the type argument.
	CHECK(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, sv) == 0);
	CHECK(fcntl(sv[0], F_GETFL) & O_NONBLOCK);
	CHECK(fcntl(sv[1], F_GETFD) & FD_CLOEXEC);
	CHECK(read(sv[0], buf, sizeof buf) == -1 && errno == EAGAIN);
	// Fill the buffer until a non-blocking write would block.
	static char big[65536];
	long total = 0;
	for (;;) {
		ssize_t n = write(sv[0], big, sizeof big);
		if (n < 0) {
			CHECK(errno == EAGAIN);
			break;
		}
		total += n;
		CHECK(total < 64L * 1024 * 1024);
	}
	CHECK(total > 0);
	close(sv[0]);
	close(sv[1]);
	printf("stream ok\n");
}

static void dgram(void)
{
	int sv[2];
	common(SOCK_DGRAM, sv);
	CHECK(write(sv[0], "abc", 3) == 3);
	CHECK(write(sv[0], "defgh", 5) == 5);
	char buf[16];
	CHECK(read(sv[1], buf, sizeof buf) == 3 && memcmp(buf, "abc", 3) == 0);
	CHECK(recv(sv[1], buf, 2, MSG_TRUNC) == 5);
	CHECK(write(sv[0], "", 0) == 0);
	CHECK(read(sv[1], buf, sizeof buf) == 0);
	close(sv[1]);
	CHECK(send(sv[0], "x", 1, MSG_NOSIGNAL) == -1 && errno == ECONNREFUSED);
	close(sv[0]);
	printf("dgram ok\n");
}

static void seqpacket(void)
{
	int sv[2];
	common(SOCK_SEQPACKET, sv);
	CHECK(write(sv[0], "abc", 3) == 3);
	CHECK(write(sv[0], "defgh", 5) == 5);
	char buf[16];
	CHECK(read(sv[1], buf, sizeof buf) == 3 && memcmp(buf, "abc", 3) == 0);
	// A short read drops the rest of the record.
	struct iovec iov = { .iov_base = buf, .iov_len = 2 };
	struct msghdr msg = { .msg_iov = &iov, .msg_iovlen = 1 };
	CHECK(recvmsg(sv[1], &msg, 0) == 2 && (msg.msg_flags & MSG_TRUNC));
	CHECK(recv(sv[1], buf, sizeof buf, MSG_DONTWAIT) == -1 && errno == EAGAIN);
	close(sv[0]);
	CHECK(read(sv[1], buf, sizeof buf) == 0);
	close(sv[1]);
	printf("seqpacket ok\n");
}

int main(void)
{
	stream();
	dgram();
	seqpacket();

	int sv[2];
	CHECK(socketpair(AF_INET, SOCK_STREAM, 0, sv) == -1 && errno == EOPNOTSUPP);
	CHECK(socketpair(AF_UNIX, SOCK_STREAM, 2, sv) == -1 && errno == EPROTONOSUPPORT);
	CHECK(socketpair(AF_UNIX, 99, 0, sv) == -1 && errno == EINVAL);
	printf("ok\n");
	return 0;
}
