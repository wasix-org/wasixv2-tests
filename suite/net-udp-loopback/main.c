// UDP over loopback: datagram boundaries, sender addresses, MSG_PEEK, MSG_TRUNC, connected
// sockets and recvmsg.
//#ExpectedStdout: ok
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
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

static struct sockaddr_in bound(int fd)
{
	struct sockaddr_in a = { .sin_family = AF_INET };
	a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	CHECK(bind(fd, (struct sockaddr *)&a, sizeof a) == 0);
	socklen_t len = sizeof a;
	CHECK(getsockname(fd, (struct sockaddr *)&a, &len) == 0);
	CHECK(a.sin_port != 0);
	return a;
}

int main(void)
{
	int a = socket(AF_INET, SOCK_DGRAM, 0);
	int b = socket(AF_INET, SOCK_DGRAM, 0);
	CHECK(a >= 0 && b >= 0);
	struct sockaddr_in aa = bound(a), ba = bound(b);

	CHECK(sendto(a, "one", 3, 0, (struct sockaddr *)&ba, sizeof ba) == 3);
	CHECK(sendto(a, "second", 6, 0, (struct sockaddr *)&ba, sizeof ba) == 6);

	// FIONREAD on a UDP socket is the size of the next datagram.
	int avail = 0;
	CHECK(ioctl(b, FIONREAD, &avail) == 0 && avail == 3);

	char buf[64];
	struct sockaddr_in from;
	socklen_t len = sizeof from;
	CHECK(recvfrom(b, buf, sizeof buf, MSG_PEEK, (struct sockaddr *)&from, &len) == 3);
	CHECK(from.sin_port == aa.sin_port && from.sin_addr.s_addr == htonl(INADDR_LOOPBACK));
	len = sizeof from;
	CHECK(recvfrom(b, buf, sizeof buf, 0, (struct sockaddr *)&from, &len) == 3);
	CHECK(memcmp(buf, "one", 3) == 0);

	// A short buffer truncates; MSG_TRUNC returns the real length.
	CHECK(recv(b, buf, 2, MSG_TRUNC) == 6);
	CHECK(memcmp(buf, "se", 2) == 0);

	// Nothing left: non-blocking receive is EAGAIN.
	CHECK(recv(b, buf, sizeof buf, MSG_DONTWAIT) == -1 && errno == EAGAIN);

	// recvmsg reports truncation in msg_flags.
	CHECK(sendto(a, "truncated", 9, 0, (struct sockaddr *)&ba, sizeof ba) == 9);
	struct iovec iov = { .iov_base = buf, .iov_len = 4 };
	struct msghdr msg = { .msg_name = &from, .msg_namelen = sizeof from, .msg_iov = &iov,
			      .msg_iovlen = 1 };
	CHECK(recvmsg(b, &msg, 0) == 4);
	CHECK(msg.msg_flags & MSG_TRUNC);
	CHECK(msg.msg_namelen == sizeof from && from.sin_port == aa.sin_port);

	// A connected socket uses send/recv and only hears from its peer.
	CHECK(connect(a, (struct sockaddr *)&ba, sizeof ba) == 0);
	struct sockaddr_in p;
	len = sizeof p;
	CHECK(getpeername(a, (struct sockaddr *)&p, &len) == 0 && p.sin_port == ba.sin_port);
	CHECK(send(a, "hi", 2, 0) == 2);
	CHECK(recv(b, buf, sizeof buf, 0) == 2 && memcmp(buf, "hi", 2) == 0);

	int c = socket(AF_INET, SOCK_DGRAM, 0);
	CHECK(c >= 0);
	struct sockaddr_in ca = bound(c);
	(void)ca;
	CHECK(sendto(c, "stranger", 8, 0, (struct sockaddr *)&aa, sizeof aa) == 8);
	CHECK(sendto(b, "peer", 4, 0, (struct sockaddr *)&aa, sizeof aa) == 4);
	CHECK(recv(a, buf, sizeof buf, 0) == 4 && memcmp(buf, "peer", 4) == 0);

	// Zero-length datagrams exist.
	CHECK(send(a, "", 0, 0) == 0);
	CHECK(recv(b, buf, sizeof buf, 0) == 0);

	// Connecting to AF_UNSPEC dissolves the association.
	struct sockaddr unspec = { .sa_family = AF_UNSPEC };
	CHECK(connect(a, &unspec, sizeof unspec) == 0);
	len = sizeof p;
	CHECK(getpeername(a, (struct sockaddr *)&p, &len) == -1 && errno == ENOTCONN);
	CHECK(send(a, "x", 1, 0) == -1 && errno == EDESTADDRREQ);

	close(a);
	close(b);
	close(c);
	printf("ok\n");
	return 0;
}
