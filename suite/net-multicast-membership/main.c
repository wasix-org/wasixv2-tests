// Multicast group membership on loopback: IP_ADD_MEMBERSHIP / IP_DROP_MEMBERSHIP (EINVAL for
// a unicast group, EADDRINUSE twice, EADDRNOTAVAIL for no membership), the IP_MULTICAST_*
// options (a byte is enough, TTL -1 is the default, IF by address), /proc/net/igmp listing a
// joined group with its users, delivery of a datagram to a group to every socket bound to the
// port that allows it (membership or IP_MULTICAST_ALL) and not to one with IP_MULTICAST_ALL
// off; IPv6 memberships on loopback, from which no IPv6 multicast leaves (no ff00::/8 route).
//#ExpectedStdout: ok
#include <arpa/inet.h>
#include <errno.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef IP_MULTICAST_ALL
#define IP_MULTICAST_ALL 49
#endif

#define CHECK(c)                                                                     \
	do {                                                                         \
		if (!(c)) {                                                          \
			printf("FAIL %s:%d: %s (errno=%d %s)\n", __FILE__, __LINE__, \
			       #c, errno, strerror(errno));                          \
			exit(1);                                                     \
		}                                                                    \
	} while (0)

static int bound4(int port)
{
	int fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0), one = 1;
	CHECK(fd >= 0);
	CHECK(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one) == 0);
	struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(port) };
	CHECK(bind(fd, (struct sockaddr *)&a, sizeof a) == 0);
	return fd;
}

// The users of `group` (as /proc/net/igmp prints it) on lo, 0 if lo is not in it.
static int igmp_users(const char *hex)
{
	FILE *f = fopen("/proc/net/igmp", "r");
	CHECK(f != NULL);
	char line[256], dev[32] = "";
	int users = 0;
	while (fgets(line, sizeof line, f)) {
		char name[32], group[16];
		int idx, n;
		if (sscanf(line, "%d %31s", &idx, name) == 2 && line[0] != '\t') {
			snprintf(dev, sizeof dev, "%s", name);
			continue;
		}
		if (sscanf(line, " %15s %d", group, &n) == 2 && !strcmp(dev, "lo") &&
		    !strcmp(group, hex))
			users = n;
	}
	fclose(f);
	return users;
}

static void ipv4(void)
{
	int s = socket(AF_INET, SOCK_DGRAM, 0);
	CHECK(s >= 0);
	int v = -1;
	unsigned char c = 7;
	socklen_t len = sizeof v;
	CHECK(getsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, &v, &len) == 0 && v == 1);
	CHECK(setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, &c, 1) == 0);
	len = sizeof v;
	CHECK(getsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, &v, &len) == 0 && v == 7);
	v = -1;
	CHECK(setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, &v, sizeof v) == 0);
	len = sizeof v;
	CHECK(getsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, &v, &len) == 0 && v == 1);
	v = 256;
	CHECK(setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, &v, sizeof v) == -1 && errno == EINVAL);
	c = 0;
	CHECK(setsockopt(s, IPPROTO_IP, IP_MULTICAST_LOOP, &c, 1) == 0);
	len = sizeof v;
	CHECK(getsockopt(s, IPPROTO_IP, IP_MULTICAST_LOOP, &v, &len) == 0 && v == 0);
	c = 1;
	CHECK(setsockopt(s, IPPROTO_IP, IP_MULTICAST_LOOP, &c, 1) == 0);
	struct in_addr lo = { htonl(INADDR_LOOPBACK) }, got;
	CHECK(setsockopt(s, IPPROTO_IP, IP_MULTICAST_IF, &lo, sizeof lo) == 0);
	len = sizeof got;
	CHECK(getsockopt(s, IPPROTO_IP, IP_MULTICAST_IF, &got, &len) == 0 &&
	      got.s_addr == lo.s_addr);

	int member = bound4(6000), other = bound4(6000), no_all = bound4(6000);
	struct ip_mreqn mreq = { .imr_ifindex = (int)if_nametoindex("lo") };
	mreq.imr_multiaddr.s_addr = inet_addr("10.0.0.1");
	CHECK(setsockopt(member, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof mreq) == -1 &&
	      errno == EINVAL);
	mreq.imr_multiaddr.s_addr = inet_addr("239.1.2.3");
	CHECK(setsockopt(member, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof mreq) == 0);
	CHECK(setsockopt(member, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof mreq) == -1 &&
	      errno == EADDRINUSE);
	CHECK(igmp_users("030201EF") == 1);
	int off = 0;
	CHECK(setsockopt(no_all, IPPROTO_IP, IP_MULTICAST_ALL, &off, sizeof off) == 0);

	struct sockaddr_in to = { .sin_family = AF_INET, .sin_port = htons(6000) };
	to.sin_addr.s_addr = inet_addr("239.1.2.3");
	CHECK(sendto(s, "m", 1, 0, (struct sockaddr *)&to, sizeof to) == 1);
	char buf[8];
	struct sockaddr_in from;
	socklen_t flen = sizeof from;
	CHECK(recvfrom(member, buf, sizeof buf, 0, (struct sockaddr *)&from, &flen) == 1);
	CHECK(from.sin_addr.s_addr == htonl(INADDR_LOOPBACK));
	CHECK(recv(other, buf, sizeof buf, 0) == 1);
	CHECK(recv(no_all, buf, sizeof buf, 0) == -1 && errno == EAGAIN);

	CHECK(setsockopt(member, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof mreq) == 0);
	CHECK(setsockopt(member, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof mreq) == -1 &&
	      errno == EADDRNOTAVAIL);
	CHECK(igmp_users("030201EF") == 0);
	// Nobody holds the group: lo is out of it, and the datagram reaches no one.
	CHECK(sendto(s, "x", 1, 0, (struct sockaddr *)&to, sizeof to) == 1);
	CHECK(recv(other, buf, sizeof buf, 0) == -1 && errno == EAGAIN);
	close(member);
	close(other);
	close(no_all);
	close(s);
}

static void ipv6(void)
{
	int lo = (int)if_nametoindex("lo");
	int r = socket(AF_INET6, SOCK_DGRAM | SOCK_NONBLOCK, 0);
	CHECK(r >= 0);
	struct sockaddr_in6 a = { .sin6_family = AF_INET6, .sin6_port = htons(6001) };
	CHECK(bind(r, (struct sockaddr *)&a, sizeof a) == 0);
	struct ipv6_mreq mreq = { .ipv6mr_interface = (unsigned)lo };
	inet_pton(AF_INET6, "ff02::1:3", &mreq.ipv6mr_multiaddr);
	CHECK(setsockopt(r, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP, &mreq, sizeof mreq) == 0);
	CHECK(setsockopt(r, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP, &mreq, sizeof mreq) == -1 &&
	      errno == EADDRINUSE);

	int s = socket(AF_INET6, SOCK_DGRAM, 0), v = 2;
	CHECK(s >= 0);
	CHECK(setsockopt(s, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, &v, sizeof v) == -1 &&
	      errno == EINVAL);
	CHECK(setsockopt(s, IPPROTO_IPV6, IPV6_MULTICAST_IF, &lo, sizeof lo) == 0);
	// Loopback has no ff00::/8 route: IPv6 multicast does not leave by it.
	struct sockaddr_in6 to = { .sin6_family = AF_INET6, .sin6_port = htons(6001) };
	inet_pton(AF_INET6, "ff02::1:3", &to.sin6_addr);
	CHECK(sendto(s, "6", 1, 0, (struct sockaddr *)&to, sizeof to) == -1 &&
	      errno == ENETUNREACH);
	CHECK(setsockopt(r, IPPROTO_IPV6, IPV6_DROP_MEMBERSHIP, &mreq, sizeof mreq) == 0);
	CHECK(setsockopt(r, IPPROTO_IPV6, IPV6_DROP_MEMBERSHIP, &mreq, sizeof mreq) == -1 &&
	      errno == EADDRNOTAVAIL);
	close(r);
	close(s);
}

int main(void)
{
	ipv4();
	ipv6();
	printf("ok\n");
	return 0;
}
