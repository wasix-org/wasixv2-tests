// The error queue of a UDP socket with IP_RECVERR: a datagram to a closed loopback port is
// refused by an ICMP port unreachable, which ip_icmp_error queues and makes the pending error
// even though the socket is unconnected (__udp4_lib_err). recvmsg(MSG_ERRQUEUE) returns the
// datagram, its destination as the address and an IP_RECVERR control message holding the
// sock_extended_err and the offender (ip_recv_error); it never blocks, and taking the last
// ICMP entry clears SO_ERROR (sock_dequeue_err_skb).
//#ExpectedStdout: ok
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
// linux/errqueue.h's scm_timestamping needs struct timespec.
#include <time.h>
#include <linux/errqueue.h>
#include <netinet/in.h>
#include <netinet/ip_icmp.h>
#include <poll.h>
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

struct entry {
	ssize_t n;
	int flags;
	char data[64];
	struct sockaddr_in from;
	struct sock_extended_err ee;
	struct sockaddr_in offender;
	int found;
};

// One blocking recvmsg(MSG_ERRQUEUE).
static void read_error(int fd, struct entry *e)
{
	union {
		char buf[CMSG_SPACE(sizeof(struct sock_extended_err) + sizeof(struct sockaddr_in6))];
		struct cmsghdr align;
	} u;
	memset(e, 0, sizeof *e);
	struct iovec iov = { .iov_base = e->data, .iov_len = sizeof e->data };
	struct msghdr msg = {
		.msg_name = &e->from,
		.msg_namelen = sizeof e->from,
		.msg_iov = &iov,
		.msg_iovlen = 1,
		.msg_control = u.buf,
		.msg_controllen = sizeof u.buf,
	};
	e->n = recvmsg(fd, &msg, MSG_ERRQUEUE);
	if (e->n < 0)
		return;
	e->flags = msg.msg_flags;
	CHECK(msg.msg_namelen == sizeof e->from);
	for (struct cmsghdr *c = CMSG_FIRSTHDR(&msg); c; c = CMSG_NXTHDR(&msg, c)) {
		if (c->cmsg_level != SOL_IP || c->cmsg_type != IP_RECVERR)
			continue;
		struct sock_extended_err *ee = (struct sock_extended_err *)CMSG_DATA(c);
		memcpy(&e->ee, ee, sizeof e->ee);
		memcpy(&e->offender, SO_EE_OFFENDER(ee), sizeof e->offender);
		e->found = 1;
	}
}

static int so_error(int fd)
{
	int err = -1;
	socklen_t len = sizeof err;
	CHECK(getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0);
	return err;
}

int main(void)
{
	int s = socket(AF_INET, SOCK_DGRAM, 0);
	CHECK(s >= 0);
	struct sockaddr_in local = { .sin_family = AF_INET,
				     .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
	CHECK(bind(s, (struct sockaddr *)&local, sizeof local) == 0);
	int on = 1;
	CHECK(setsockopt(s, SOL_IP, IP_RECVERR, &on, sizeof on) == 0);

	// A blocking read of the empty queue fails at once.
	struct entry e;
	read_error(s, &e);
	CHECK(e.n == -1 && errno == EAGAIN);

	// A loopback port nobody holds.
	int closed = socket(AF_INET, SOCK_DGRAM, 0);
	CHECK(closed >= 0);
	struct sockaddr_in dst = local;
	CHECK(bind(closed, (struct sockaddr *)&dst, sizeof dst) == 0);
	socklen_t len = sizeof dst;
	CHECK(getsockname(closed, (struct sockaddr *)&dst, &len) == 0);
	close(closed);

	CHECK(sendto(s, "ping", 4, 0, (struct sockaddr *)&dst, sizeof dst) == 4);
	struct pollfd p = { .fd = s, .events = POLLIN };
	CHECK(poll(&p, 1, 5000) == 1 && (p.revents & POLLERR) && !(p.revents & POLLIN));

	read_error(s, &e);
	CHECK(e.n == 4 && memcmp(e.data, "ping", 4) == 0);
	CHECK(e.flags & MSG_ERRQUEUE);
	CHECK(e.from.sin_family == AF_INET && e.from.sin_port == dst.sin_port &&
	      e.from.sin_addr.s_addr == htonl(INADDR_LOOPBACK));
	CHECK(e.found);
	CHECK(e.ee.ee_errno == ECONNREFUSED);
	CHECK(e.ee.ee_origin == SO_EE_ORIGIN_ICMP);
	CHECK(e.ee.ee_type == ICMP_DEST_UNREACH && e.ee.ee_code == ICMP_PORT_UNREACH);
	CHECK(e.offender.sin_family == AF_INET &&
	      e.offender.sin_addr.s_addr == htonl(INADDR_LOOPBACK));
	// The last ICMP entry taken, no error is pending and nothing is queued.
	CHECK(so_error(s) == 0);
	read_error(s, &e);
	CHECK(e.n == -1 && errno == EAGAIN);
	p.revents = 0;
	CHECK(poll(&p, 1, 0) == 0);

	// SO_ERROR takes the pending error and leaves the entry queued.
	CHECK(sendto(s, "pong", 4, 0, (struct sockaddr *)&dst, sizeof dst) == 4);
	CHECK(poll(&p, 1, 5000) == 1 && (p.revents & POLLERR));
	CHECK(so_error(s) == ECONNREFUSED);
	CHECK(so_error(s) == 0);
	p.revents = 0;
	CHECK(poll(&p, 1, 0) == 1 && (p.revents & POLLERR));
	read_error(s, &e);
	CHECK(e.n == 4 && memcmp(e.data, "pong", 4) == 0 && e.found);

	// Without IP_RECVERR an unconnected socket never hears of a refused datagram.
	on = 0;
	CHECK(setsockopt(s, SOL_IP, IP_RECVERR, &on, sizeof on) == 0);
	CHECK(sendto(s, "lost", 4, 0, (struct sockaddr *)&dst, sizeof dst) == 4);
	usleep(10000);
	CHECK(so_error(s) == 0);
	read_error(s, &e);
	CHECK(e.n == -1 && errno == EAGAIN);
	printf("ok\n");
	return 0;
}
