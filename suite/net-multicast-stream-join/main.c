// Multicast joins on TCP sockets (net/ipv4/ip_sockglue.c, net/ipv6/ipv6_sockglue.c): only
// IP_ADD_MEMBERSHIP and IP_DROP_MEMBERSHIP refuse a connection-oriented socket with EPROTO,
// before looking at the length, and IPV6_ADD_MEMBERSHIP and IPV6_DROP_MEMBERSHIP after it
// (EINVAL for a short value). MCAST_JOIN_GROUP joins as on any socket: loopback lists the
// group in /proc/net/igmp until the socket is closed (ip_mc_drop_socket).
//#ExpectedStdout: ok
#include <errno.h>
#include <net/if.h>
#include <netinet/in.h>
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

// Whether /proc/net/igmp lists 239.7.7.7 (in hex, as the kernel prints the __be32).
static int listed(void)
{
	FILE *f = fopen("/proc/net/igmp", "r");
	CHECK(f != NULL);
	char line[256];
	int found = 0;
	char want[16];
	struct in_addr g = { .s_addr = htonl(0xef070707) };
	snprintf(want, sizeof want, "%08X", g.s_addr);
	while (fgets(line, sizeof line, f))
		if (strstr(line, want))
			found = 1;
	fclose(f);
	return found;
}

int main(void)
{
	int t = socket(AF_INET, SOCK_STREAM, 0);
	CHECK(t >= 0);
	struct ip_mreq mreq = { .imr_multiaddr.s_addr = htonl(0xef070707),
				.imr_interface.s_addr = htonl(INADDR_LOOPBACK) };
	CHECK(setsockopt(t, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof mreq) == -1 &&
	      errno == EPROTO);
	CHECK(setsockopt(t, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, 4) == -1 && errno == EPROTO);
	CHECK(setsockopt(t, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof mreq) == -1 &&
	      errno == EPROTO);

	int t6 = socket(AF_INET6, SOCK_STREAM, 0);
	CHECK(t6 >= 0);
	struct ipv6_mreq mreq6 = { .ipv6mr_interface = 1 };
	mreq6.ipv6mr_multiaddr.s6_addr[0] = 0xff;
	mreq6.ipv6mr_multiaddr.s6_addr[1] = 0x05;
	mreq6.ipv6mr_multiaddr.s6_addr[15] = 7;
	CHECK(setsockopt(t6, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP, &mreq6, 4) == -1 && errno == EINVAL);
	CHECK(setsockopt(t6, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP, &mreq6, sizeof mreq6) == -1 &&
	      errno == EPROTO);
	close(t6);

	struct group_req req = { .gr_interface = if_nametoindex("lo") };
	CHECK(req.gr_interface != 0);
	struct sockaddr_in *sin = (struct sockaddr_in *)&req.gr_group;
	sin->sin_family = AF_INET;
	sin->sin_addr.s_addr = htonl(0xef070707);
	CHECK(!listed());
	CHECK(setsockopt(t, IPPROTO_IP, MCAST_JOIN_GROUP, &req, sizeof req) == 0);
	CHECK(listed());
	close(t);
	CHECK(!listed());
	printf("ok\n");
	return 0;
}
