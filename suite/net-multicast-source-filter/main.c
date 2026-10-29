// Multicast source filters on loopback (ip_mc_source, ip_mc_msfilter): a filter needs a
// membership (EINVAL), IP_ADD_SOURCE_MEMBERSHIP makes one that takes only its sources
// (EADDRNOTAVAIL twice, EINVAL for a source of the other mode), the interface then lets in
// only what some membership's filter allows, so a socket with just IP_MULTICAST_ALL hears
// that alone; IP_BLOCK_SOURCE keeps a sender out of an EXCLUDE membership until
// IP_UNBLOCK_SOURCE; IP_MSFILTER and MCAST_MSFILTER read the filter back and set one whole
// (ENOBUFS past igmp_max_msf, 10); dropping the last source of an INCLUDE filter leaves the
// group. Senders are 127.0.0.1 and 127.0.0.2.
//#ExpectedStdout: ok
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
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

#define PORT 47810
#define GROUP "239.2.2.2"

static int bound(const char *ip, int port)
{
	int s = socket(AF_INET, SOCK_DGRAM, 0), one = 1;
	CHECK(s >= 0 && setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one) == 0);
	struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(port) };
	inet_pton(AF_INET, ip, &a.sin_addr);
	CHECK(bind(s, (struct sockaddr *)&a, sizeof a) == 0);
	return s;
}

static void send_group(int s, const char *data)
{
	struct sockaddr_in to = { .sin_family = AF_INET, .sin_port = htons(PORT) };
	inet_pton(AF_INET, GROUP, &to.sin_addr);
	CHECK(sendto(s, data, strlen(data), 0, (struct sockaddr *)&to, sizeof to) ==
	      (ssize_t)strlen(data));
}

// What `s` received within 300 ms, "" for nothing.
static const char *got(int s)
{
	static char buf[16];
	struct pollfd p = { .fd = s, .events = POLLIN };
	if (poll(&p, 1, 300) != 1)
		return "";
	ssize_t n = recv(s, buf, sizeof buf - 1, 0);
	CHECK(n >= 0);
	buf[n] = 0;
	return buf;
}

static struct ip_mreq_source req(const char *source)
{
	struct ip_mreq_source r;
	inet_pton(AF_INET, GROUP, &r.imr_multiaddr);
	inet_pton(AF_INET, "127.0.0.1", &r.imr_interface);
	inet_pton(AF_INET, source, &r.imr_sourceaddr);
	return r;
}

static int opt(int s, int name, const char *source)
{
	struct ip_mreq_source r = req(source);
	return setsockopt(s, IPPROTO_IP, name, &r, sizeof r);
}

int main(void)
{
	int ssm = bound("0.0.0.0", PORT), all = bound("0.0.0.0", PORT);
	int blocker = bound("0.0.0.0", PORT);
	int a1 = bound("127.0.0.1", 0), a2 = bound("127.0.0.2", 0);
	struct in_addr lo = { htonl(INADDR_LOOPBACK) };
	CHECK(setsockopt(a1, IPPROTO_IP, IP_MULTICAST_IF, &lo, sizeof lo) == 0);
	CHECK(setsockopt(a2, IPPROTO_IP, IP_MULTICAST_IF, &lo, sizeof lo) == 0);

	CHECK(opt(ssm, IP_BLOCK_SOURCE, "127.0.0.2") == -1 && errno == EINVAL);
	struct ip_mreq_source r = req("127.0.0.1");
	CHECK(setsockopt(ssm, IPPROTO_IP, IP_ADD_SOURCE_MEMBERSHIP, &r, 8) == -1 && errno == EINVAL);
	CHECK(opt(ssm, IP_ADD_SOURCE_MEMBERSHIP, "127.0.0.1") == 0);
	CHECK(opt(ssm, IP_ADD_SOURCE_MEMBERSHIP, "127.0.0.1") == -1 && errno == EADDRNOTAVAIL);
	CHECK(opt(ssm, IP_BLOCK_SOURCE, "127.0.0.2") == -1 && errno == EINVAL);
	send_group(a1, "1");
	send_group(a2, "2");
	CHECK(!strcmp(got(ssm), "1") && !strcmp(got(ssm), ""));
	CHECK(!strcmp(got(all), "1") && !strcmp(got(all), ""));
	CHECK(!strcmp(got(blocker), "1") && !strcmp(got(blocker), ""));

	// An EXCLUDE membership that blocks 127.0.0.2: nobody hears it until it is unblocked.
	struct ip_mreqn m = { .imr_address = lo };
	inet_pton(AF_INET, GROUP, &m.imr_multiaddr);
	CHECK(setsockopt(blocker, IPPROTO_IP, IP_ADD_MEMBERSHIP, &m, sizeof m) == 0);
	CHECK(opt(blocker, IP_BLOCK_SOURCE, "127.0.0.2") == 0);
	send_group(a2, "2");
	CHECK(!strcmp(got(all), "") && !strcmp(got(blocker), ""));
	CHECK(opt(blocker, IP_UNBLOCK_SOURCE, "127.0.0.2") == 0);
	CHECK(opt(blocker, IP_UNBLOCK_SOURCE, "127.0.0.2") == -1 && errno == EADDRNOTAVAIL);
	send_group(a2, "2");
	CHECK(!strcmp(got(blocker), "2") && !strcmp(got(all), "2") && !strcmp(got(ssm), ""));

	// IP_MSFILTER reads the filter back.
	char buf[IP_MSFILTER_SIZE(4)];
	struct ip_msfilter *msf = (struct ip_msfilter *)buf;
	memset(buf, 0, sizeof buf);
	msf->imsf_multiaddr = r.imr_multiaddr;
	msf->imsf_interface = lo;
	msf->imsf_numsrc = 4;
	socklen_t len = sizeof buf;
	CHECK(getsockopt(ssm, IPPROTO_IP, IP_MSFILTER, msf, &len) == 0);
	CHECK(len == IP_MSFILTER_SIZE(1) && msf->imsf_fmode == MCAST_INCLUDE &&
	      msf->imsf_numsrc == 1 && msf->imsf_slist[0].s_addr == lo.s_addr);
	// MCAST_MSFILTER sets one whole: INCLUDE both senders; igmp_max_msf is 10.
	char gbuf[GROUP_FILTER_SIZE(11)];
	struct group_filter *gf = (struct group_filter *)gbuf;
	memset(gbuf, 0, sizeof gbuf);
	gf->gf_interface = 1;
	struct sockaddr_in *g = (struct sockaddr_in *)&gf->gf_group;
	g->sin_family = AF_INET;
	g->sin_addr = r.imr_multiaddr;
	gf->gf_fmode = MCAST_INCLUDE;
	gf->gf_numsrc = 11;
	CHECK(setsockopt(ssm, IPPROTO_IP, MCAST_MSFILTER, gf, GROUP_FILTER_SIZE(11)) == -1 &&
	      errno == ENOBUFS);
	gf->gf_numsrc = 2;
	for (int i = 0; i < 2; i++) {
		struct sockaddr_in *s = (struct sockaddr_in *)&gf->gf_slist[i];
		s->sin_family = AF_INET;
		inet_pton(AF_INET, i ? "127.0.0.2" : "127.0.0.1", &s->sin_addr);
	}
	CHECK(setsockopt(ssm, IPPROTO_IP, MCAST_MSFILTER, gf, GROUP_FILTER_SIZE(2)) == 0);
	send_group(a2, "2");
	CHECK(!strcmp(got(ssm), "2"));
	// Dropping every source of the INCLUDE filter leaves the group.
	CHECK(opt(ssm, IP_DROP_SOURCE_MEMBERSHIP, "127.0.0.1") == 0);
	CHECK(opt(ssm, IP_DROP_SOURCE_MEMBERSHIP, "127.0.0.2") == 0);
	CHECK(setsockopt(ssm, IPPROTO_IP, IP_DROP_MEMBERSHIP, &m, sizeof m) == -1 &&
	      errno == EADDRNOTAVAIL);
	printf("ok\n");
	return 0;
}
