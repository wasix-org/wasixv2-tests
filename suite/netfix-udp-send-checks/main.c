// UDP sends fail in udp_sendmsg's order: the socket is bound first, then the address is checked
// (EDESTADDRREQ, EINVAL for port 0) before the shut down sender's EPIPE. Control messages are
// checked as __sock_cmsg_send and ip_cmsg_send check them, IP_PKTINFO's address becomes the
// datagram's source and UDP_SEGMENT splits a send into datagrams.
//#ExpectedStdout: ok
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/udp.h>
#include <stdint.h>
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

// One control message of `len` bytes of `data` at `level`/`type`.
static ssize_t send_ctl(int fd, const void *payload, size_t n, struct sockaddr_in *to,
			int level, int type, const void *data, size_t len)
{
	union {
		char buf[CMSG_SPACE(32)];
		struct cmsghdr align;
	} u;
	memset(&u, 0, sizeof u);
	struct iovec iov = { .iov_base = (void *)payload, .iov_len = n };
	struct msghdr msg = {
		.msg_name = to,
		.msg_namelen = sizeof *to,
		.msg_iov = &iov,
		.msg_iovlen = 1,
		.msg_control = u.buf,
		.msg_controllen = CMSG_SPACE(len),
	};
	struct cmsghdr *c = CMSG_FIRSTHDR(&msg);
	c->cmsg_level = level;
	c->cmsg_type = type;
	c->cmsg_len = CMSG_LEN(len);
	memcpy(CMSG_DATA(c), data, len);
	return sendmsg(fd, &msg, 0);
}

int main(void)
{
	int r = socket(AF_INET, SOCK_DGRAM, 0);
	CHECK(r >= 0);
	struct sockaddr_in to = { .sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_ANY) };
	CHECK(bind(r, (struct sockaddr *)&to, sizeof to) == 0);
	socklen_t len = sizeof to;
	CHECK(getsockname(r, (struct sockaddr *)&to, &len) == 0);
	to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	int s = socket(AF_INET, SOCK_DGRAM, 0);
	CHECK(s >= 0);
	CHECK(shutdown(s, SHUT_WR) == -1 && errno == ENOTCONN);
	CHECK(send(s, "x", 1, 0) == -1 && errno == EDESTADDRREQ);
	struct sockaddr_in local;
	len = sizeof local;
	CHECK(getsockname(s, (struct sockaddr *)&local, &len) == 0 && local.sin_port != 0);
	struct sockaddr_in zero = to;
	zero.sin_port = 0;
	CHECK(sendto(s, "x", 1, 0, (struct sockaddr *)&zero, sizeof zero) == -1 && errno == EINVAL);
	CHECK(sendto(s, "x", 1, 0, (struct sockaddr *)&to, sizeof to) == -1 && errno == EPIPE);
	close(s);

	s = socket(AF_INET, SOCK_DGRAM, 0);
	CHECK(s >= 0);
	// IP_PKTINFO: the source address the receiver sees.
	struct in_pktinfo info = { .ipi_spec_dst.s_addr = htonl(0x7f000002) };
	CHECK(send_ctl(s, "a", 1, &to, IPPROTO_IP, IP_PKTINFO, &info, sizeof info) == 1);
	char buf[16];
	struct sockaddr_in from;
	len = sizeof from;
	CHECK(recvfrom(r, buf, sizeof buf, 0, (struct sockaddr *)&from, &len) == 1);
	CHECK(from.sin_addr.s_addr == htonl(0x7f000002));
	// Not an address of the machine; no such interface.
	info.ipi_spec_dst.s_addr = inet_addr("10.9.9.9");
	CHECK(send_ctl(s, "a", 1, &to, IPPROTO_IP, IP_PKTINFO, &info, sizeof info) == -1 &&
	      errno == EINVAL);
	info.ipi_spec_dst.s_addr = 0;
	info.ipi_ifindex = 999;
	CHECK(send_ctl(s, "a", 1, &to, IPPROTO_IP, IP_PKTINFO, &info, sizeof info) == -1 &&
	      errno == ENODEV);
	// IP_TTL and IP_TOS are range checked; unknown types are EINVAL.
	int ttl = 0;
	CHECK(send_ctl(s, "a", 1, &to, IPPROTO_IP, IP_TTL, &ttl, sizeof ttl) == -1 &&
	      errno == EINVAL);
	ttl = 64;
	CHECK(send_ctl(s, "a", 1, &to, IPPROTO_IP, IP_TTL, &ttl, sizeof ttl) == 1);
	uint8_t tos = 0x10;
	CHECK(send_ctl(s, "a", 1, &to, IPPROTO_IP, IP_TOS, &tos, sizeof tos) == 1);
	int junk = 0;
	CHECK(send_ctl(s, "a", 1, &to, IPPROTO_IP, 4242, &junk, sizeof junk) == -1 &&
	      errno == EINVAL);
	// Socket-level messages: SO_MARK needs CAP_NET_ADMIN, an unknown type is EINVAL.
	uint32_t mark = 1;
	CHECK(send_ctl(s, "a", 1, &to, SOL_SOCKET, SO_MARK, &mark, sizeof mark) == -1 &&
	      errno == EPERM);
	CHECK(send_ctl(s, "a", 1, &to, SOL_SOCKET, 4242, &mark, sizeof mark) == -1 &&
	      errno == EINVAL);
	for (int i = 0; i < 2; i++)
		CHECK(recv(r, buf, sizeof buf, 0) == 1);
	// UDP_SEGMENT: ten bytes in segments of four are three datagrams.
	uint16_t seg = 4;
	CHECK(send_ctl(s, "0123456789", 10, &to, SOL_UDP, UDP_SEGMENT, &seg, sizeof seg) == 10);
	CHECK(recv(r, buf, sizeof buf, 0) == 4 && memcmp(buf, "0123", 4) == 0);
	CHECK(recv(r, buf, sizeof buf, 0) == 4 && memcmp(buf, "4567", 4) == 0);
	CHECK(recv(r, buf, sizeof buf, 0) == 2 && memcmp(buf, "89", 2) == 0);
	CHECK(recv(r, buf, sizeof buf, MSG_DONTWAIT) == -1 && errno == EAGAIN);
	close(s);
	close(r);
	puts("ok");
	return 0;
}
