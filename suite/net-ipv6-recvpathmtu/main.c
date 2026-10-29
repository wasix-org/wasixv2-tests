// IPV6_RECVPATHMTU and IPV6_PATHMTU (net/ipv6/datagram.c ipv6_local_rxpmtu,
// ipv6_recv_rxpmtu; ipv6_sockglue.c). A UDP datagram to ::1 larger than loopback's MTU of
// 65536 with IPV6_DONTFRAG is EMSGSIZE; with IPV6_RECVPATHMTU the next recvmsg returns no data,
// ::1 as the sender and an IPV6_PATHMTU control message holding the MTU, ahead of a datagram
// already queued, and only once. IPV6_PATHMTU is ENOTCONN on an unconnected socket and the
// route's MTU on a connected one.
//#ExpectedStdout: ok
#define _GNU_SOURCE
#include <errno.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
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

static char big[65500];

// Brings lo up where it is down (a new network namespace), as far as the caller may.
static void lo_up(void)
{
	int fd = socket(AF_INET, SOCK_DGRAM, 0);
	struct ifreq ifr = { 0 };
	strcpy(ifr.ifr_name, "lo");
	CHECK(ioctl(fd, SIOCGIFFLAGS, &ifr) == 0);
	if (!(ifr.ifr_flags & IFF_UP)) {
		ifr.ifr_flags |= IFF_UP;
		CHECK(ioctl(fd, SIOCSIFFLAGS, &ifr) == 0);
	}
	close(fd);
}

int main(void)
{
	lo_up();
	int s = socket(AF_INET6, SOCK_DGRAM, 0);
	CHECK(s >= 0);
	struct sockaddr_in6 self = { .sin6_family = AF_INET6, .sin6_addr = IN6ADDR_LOOPBACK_INIT };
	CHECK(bind(s, (struct sockaddr *)&self, sizeof self) == 0);
	socklen_t len = sizeof self;
	CHECK(getsockname(s, (struct sockaddr *)&self, &len) == 0);

	struct ip6_mtuinfo info;
	len = sizeof info;
	CHECK(getsockopt(s, IPPROTO_IPV6, IPV6_PATHMTU, &info, &len) == -1 && errno == ENOTCONN);
	int one = 1, v = 0;
	CHECK(setsockopt(s, IPPROTO_IPV6, IPV6_RECVPATHMTU, &one, sizeof one) == 0);
	len = sizeof v;
	CHECK(getsockopt(s, IPPROTO_IPV6, IPV6_RECVPATHMTU, &v, &len) == 0 && v == 1);
	CHECK(setsockopt(s, IPPROTO_IPV6, IPV6_DONTFRAG, &one, sizeof one) == 0);

	CHECK(sendto(s, "q", 1, 0, (struct sockaddr *)&self, sizeof self) == 1);
	CHECK(sendto(s, big, sizeof big, 0, (struct sockaddr *)&self, sizeof self) == -1 &&
	      errno == EMSGSIZE);

	char data[16], control[256];
	struct sockaddr_in6 from = { 0 };
	struct iovec iov = { .iov_base = data, .iov_len = sizeof data };
	struct msghdr m = { .msg_name = &from, .msg_namelen = sizeof from, .msg_iov = &iov,
			    .msg_iovlen = 1, .msg_control = control,
			    .msg_controllen = sizeof control };
	CHECK(recvmsg(s, &m, 0) == 0);
	CHECK(m.msg_namelen == sizeof from && from.sin6_family == AF_INET6 &&
	      IN6_IS_ADDR_LOOPBACK(&from.sin6_addr) && from.sin6_port == 0);
	struct cmsghdr *c = CMSG_FIRSTHDR(&m);
	CHECK(c != NULL && c->cmsg_level == IPPROTO_IPV6 && c->cmsg_type == IPV6_PATHMTU);
	CHECK(c->cmsg_len == CMSG_LEN(sizeof info));
	memcpy(&info, CMSG_DATA(c), sizeof info);
	CHECK(info.ip6m_mtu == 65536);
	CHECK(info.ip6m_addr.sin6_family == AF_INET6 && IN6_IS_ADDR_LOOPBACK(&info.ip6m_addr.sin6_addr));
	CHECK(recv(s, data, sizeof data, MSG_DONTWAIT) == 1 && data[0] == 'q');
	CHECK(recv(s, data, sizeof data, MSG_DONTWAIT) == -1 && errno == EAGAIN);

	CHECK(connect(s, (struct sockaddr *)&self, sizeof self) == 0);
	memset(&info, 0xff, sizeof info);
	len = sizeof info;
	CHECK(getsockopt(s, IPPROTO_IPV6, IPV6_PATHMTU, &info, &len) == 0 && len == sizeof info);
	CHECK(info.ip6m_mtu == 65536 && info.ip6m_addr.sin6_family == 0);
	len = sizeof info - 1;
	CHECK(getsockopt(s, IPPROTO_IPV6, IPV6_PATHMTU, &info, &len) == -1 && errno == EINVAL);
	close(s);
	printf("ok\n");
	return 0;
}
