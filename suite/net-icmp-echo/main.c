// ICMP sockets on loopback. Everyone: a raw socket of protocol 0 is EPROTONOSUPPORT; without
// CAP_NET_RAW a raw ICMP socket is EPERM, and an ICMP datagram (ping) socket is EACCES unless
// net.ipv4.ping_group_range holds one of the caller's groups. Root then sets the range to its
// group and:
// a ping socket gets its echo reply from 127.0.0.1 with its own identifier (the socket's
// port), the IP_RECVTTL control message and a good checksum, refuses what is not an echo
// request (EINVAL), and pings ::1; a raw socket sees its echo request arrive and the reply
// behind an IPv4 header, ICMP_FILTER keeps the request out, and a UDP datagram to a closed
// port comes back as an ICMP port unreachable quoting it; icmp_echo_ignore_all stops replies.
//#ExpectedStdout: ok
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/icmp6.h>
#include <netinet/in.h>
#include <netinet/ip_icmp.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef ICMP_FILTER
#define ICMP_FILTER 1
#endif

#define CHECK(c)                                                                     \
	do {                                                                         \
		if (!(c)) {                                                          \
			printf("FAIL %s:%d: %s (errno=%d %s)\n", __FILE__, __LINE__, \
			       #c, errno, strerror(errno));                          \
			exit(1);                                                     \
		}                                                                    \
	} while (0)

static unsigned short csum(const void *data, size_t len)
{
	const unsigned char *p = data;
	unsigned long sum = 0;
	for (size_t i = 0; i + 1 < len; i += 2)
		sum += (unsigned)(p[i] << 8 | p[i + 1]);
	if (len & 1)
		sum += (unsigned)(p[len - 1] << 8);
	while (sum >> 16)
		sum = (sum & 0xffff) + (sum >> 16);
	return (unsigned short)~sum;
}

static size_t echo(unsigned char *buf, unsigned short id, unsigned short seq)
{
	memset(buf, 0, 16);
	buf[0] = ICMP_ECHO;
	buf[4] = id >> 8;
	buf[5] = id & 0xff;
	buf[6] = seq >> 8;
	buf[7] = seq & 0xff;
	memcpy(buf + 8, "wasixv2!", 8);
	unsigned short c = csum(buf, 16);
	buf[2] = c >> 8;
	buf[3] = c & 0xff;
	return 16;
}

static int readable(int fd, int ms)
{
	struct pollfd p = { .fd = fd, .events = POLLIN };
	return poll(&p, 1, ms) == 1;
}

static void write_sysctl(const char *path, const char *value)
{
	int fd = open(path, O_WRONLY);
	CHECK(fd >= 0);
	CHECK(write(fd, value, strlen(value)) == (ssize_t)strlen(value));
	close(fd);
}

static int in_ping_range(void)
{
	FILE *f = fopen("/proc/sys/net/ipv4/ping_group_range", "r");
	CHECK(f != NULL);
	unsigned long lo, hi;
	CHECK(fscanf(f, "%lu %lu", &lo, &hi) == 2);
	fclose(f);
	gid_t groups[64];
	int n = getgroups(64, groups);
	CHECK(n >= 0);
	if (lo <= getegid() && getegid() <= hi)
		return 1;
	for (int i = 0; i < n; i++)
		if (lo <= groups[i] && groups[i] <= hi)
			return 1;
	return 0;
}

static struct sockaddr_in lo4(void)
{
	struct sockaddr_in a = { .sin_family = AF_INET };
	a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	return a;
}

static void ping4(void)
{
	int p = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP), one = 1;
	CHECK(p >= 0);
	CHECK(setsockopt(p, IPPROTO_IP, IP_RECVTTL, &one, sizeof one) == 0);
	struct sockaddr_in to = lo4();
	unsigned char req[16], buf[64];
	CHECK(sendto(p, "\0\0\0\0\0\0\0\0", 8, 0, (struct sockaddr *)&to, sizeof to) == -1 &&
	      errno == EINVAL);
	size_t n = echo(req, 0x1234, 7);
	CHECK(sendto(p, req, n, 0, (struct sockaddr *)&to, sizeof to) == (ssize_t)n);
	struct sockaddr_in me;
	socklen_t len = sizeof me;
	CHECK(getsockname(p, (struct sockaddr *)&me, &len) == 0 && me.sin_port != 0);
	CHECK(readable(p, 2000));

	struct sockaddr_in from;
	char control[64];
	struct iovec iov = { buf, sizeof buf };
	struct msghdr msg = { .msg_name = &from, .msg_namelen = sizeof from, .msg_iov = &iov,
			      .msg_iovlen = 1, .msg_control = control,
			      .msg_controllen = sizeof control };
	ssize_t got = recvmsg(p, &msg, 0);
	CHECK(got == 16);
	CHECK(from.sin_addr.s_addr == htonl(INADDR_LOOPBACK));
	CHECK(buf[0] == ICMP_ECHOREPLY && buf[6] == 0 && buf[7] == 7);
	CHECK(((buf[4] << 8) | buf[5]) == ntohs(me.sin_port));
	CHECK(csum(buf, 16) == 0 && memcmp(buf + 8, "wasixv2!", 8) == 0);
	int ttl = -1;
	for (struct cmsghdr *c = CMSG_FIRSTHDR(&msg); c; c = CMSG_NXTHDR(&msg, c))
		if (c->cmsg_level == IPPROTO_IP && c->cmsg_type == IP_TTL)
			memcpy(&ttl, CMSG_DATA(c), sizeof ttl);
	CHECK(ttl == 64);

	// Not answered while icmp_echo_ignore_all is on.
	write_sysctl("/proc/sys/net/ipv4/icmp_echo_ignore_all", "1");
	CHECK(sendto(p, req, n, 0, (struct sockaddr *)&to, sizeof to) == (ssize_t)n);
	CHECK(!readable(p, 300));
	write_sysctl("/proc/sys/net/ipv4/icmp_echo_ignore_all", "0");
	close(p);
}

static void ping6(void)
{
	int p = socket(AF_INET6, SOCK_DGRAM, IPPROTO_ICMPV6);
	CHECK(p >= 0);
	struct sockaddr_in6 to = { .sin6_family = AF_INET6, .sin6_addr = IN6ADDR_LOOPBACK_INIT };
	unsigned char req[12] = { ICMP6_ECHO_REQUEST, 0, 0, 0, 0, 0, 0, 1, 's', 'i', 'x', '!' };
	unsigned char buf[64];
	CHECK(sendto(p, req, sizeof req, 0, (struct sockaddr *)&to, sizeof to) == sizeof req);
	CHECK(readable(p, 2000));
	struct sockaddr_in6 from;
	socklen_t len = sizeof from;
	CHECK(recvfrom(p, buf, sizeof buf, 0, (struct sockaddr *)&from, &len) == sizeof req);
	CHECK(buf[0] == ICMP6_ECHO_REPLY && memcmp(buf + 8, "six!", 4) == 0);
	CHECK(IN6_IS_ADDR_LOOPBACK(&from.sin6_addr));
	close(p);
}

static void raw4(void)
{
	int r = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
	CHECK(r >= 0);
	struct sockaddr_in to = lo4();
	unsigned char req[16], buf[256];
	size_t n = echo(req, 0x4242, 1);
	CHECK(sendto(r, req, n, 0, (struct sockaddr *)&to, sizeof to) == (ssize_t)n);
	// The request arriving on loopback, then the reply, each behind an IPv4 header.
	for (int i = 0; i < 2; i++) {
		CHECK(readable(r, 2000));
		ssize_t got = recv(r, buf, sizeof buf, 0);
		int ihl = (buf[0] & 0x0f) * 4;
		CHECK(buf[0] >> 4 == 4 && buf[9] == IPPROTO_ICMP && got == ihl + 16);
		CHECK(buf[ihl] == (i == 0 ? ICMP_ECHO : ICMP_ECHOREPLY));
		CHECK(((buf[ihl + 4] << 8) | buf[ihl + 5]) == 0x4242);
	}
	unsigned int filter = 1u << ICMP_ECHO;
	CHECK(setsockopt(r, SOL_RAW, ICMP_FILTER, &filter, sizeof filter) == 0);
	n = echo(req, 0x4242, 2);
	CHECK(sendto(r, req, n, 0, (struct sockaddr *)&to, sizeof to) == (ssize_t)n);
	CHECK(readable(r, 2000));
	ssize_t got = recv(r, buf, sizeof buf, 0);
	int ihl = (buf[0] & 0x0f) * 4;
	CHECK(got == ihl + 16 && buf[ihl] == ICMP_ECHOREPLY);
	CHECK(!readable(r, 200));

	// A UDP datagram to a port nobody holds is answered with a port unreachable.
	int u = socket(AF_INET, SOCK_DGRAM, 0);
	CHECK(u >= 0);
	struct sockaddr_in closed = lo4();
	closed.sin_port = htons(9);
	CHECK(sendto(u, "data", 4, 0, (struct sockaddr *)&closed, sizeof closed) == 4);
	CHECK(readable(r, 2000));
	got = recv(r, buf, sizeof buf, 0);
	ihl = (buf[0] & 0x0f) * 4;
	unsigned char *icmp = buf + ihl;
	CHECK(icmp[0] == ICMP_DEST_UNREACH && icmp[1] == ICMP_PORT_UNREACH);
	CHECK(csum(icmp, (size_t)got - ihl) == 0);
	unsigned char *quoted = icmp + 8;
	int qihl = (quoted[0] & 0x0f) * 4;
	CHECK(quoted[9] == IPPROTO_UDP);
	CHECK(((quoted[qihl + 2] << 8) | quoted[qihl + 3]) == 9);
	CHECK(memcmp(quoted + qihl + 8, "data", 4) == 0);
	close(u);
	close(r);
}

int main(void)
{
	CHECK(socket(AF_INET, SOCK_RAW, 0) == -1 && errno == EPROTONOSUPPORT);
	int raw = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
	if (raw < 0) {
		CHECK(errno == EPERM);
		int p = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
		CHECK(in_ping_range() ? p >= 0 : p == -1 && errno == EACCES);
		printf("ok\n");
		return 0;
	}
	close(raw);
	if (!in_ping_range())
		CHECK(socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP) == -1 && errno == EACCES);
	char range[32];
	snprintf(range, sizeof range, "%u %u", (unsigned)getegid(), (unsigned)getegid());
	write_sysctl("/proc/sys/net/ipv4/ping_group_range", range);
	ping4();
	ping6();
	raw4();
	printf("ok\n");
	return 0;
}
