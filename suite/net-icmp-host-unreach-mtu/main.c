// ICMP errors a host makes for its own packets, as root in a new network namespace with a veth
// whose peer is down. A UDP datagram to an address on the veth's subnet fails neighbour
// resolution: the host answers it with a host unreachable from its own address
// (arp_error_report), or an ICMPv6 address unreachable (ip6_link_failure), which the socket's
// error queue holds with IP_RECVERR. A datagram larger than the veth's MTU of 1500 that may
// not be fragmented (IP_PMTUDISC_DO, IPV6_DONTFRAG) is EMSGSIZE, with a local error naming the
// MTU queued; by default it would be fragmented and goes. Not root: skipped.
//#BuildEnv: WASIXCC_WASM_EXCEPTIONS=no
//#ExpectedStdout: ok
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <time.h> // struct timespec, which linux/errqueue.h uses
#include <linux/errqueue.h>
#include <linux/if_link.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/veth.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <sched.h>
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

struct req {
	struct nlmsghdr h;
	char buf[1024];
};

static void *tail(struct req *r)
{
	return (char *)&r->h + NLMSG_ALIGN(r->h.nlmsg_len);
}

static void begin(struct req *r, int type, int flags, const void *fixed, size_t len)
{
	memset(r, 0, sizeof *r);
	r->h.nlmsg_len = NLMSG_LENGTH(len);
	r->h.nlmsg_type = type;
	r->h.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK | flags;
	memcpy(NLMSG_DATA(&r->h), fixed, len);
}

static struct rtattr *attr(struct req *r, int type, const void *data, size_t len)
{
	struct rtattr *a = tail(r);
	a->rta_type = type;
	a->rta_len = RTA_LENGTH(len);
	if (len)
		memcpy(RTA_DATA(a), data, len);
	r->h.nlmsg_len = NLMSG_ALIGN(r->h.nlmsg_len) + RTA_ALIGN(a->rta_len);
	return a;
}

static void end(struct req *r, struct rtattr *a)
{
	a->rta_len = (char *)tail(r) - (char *)a;
}

// Sends `r` on a new rtnetlink socket and returns the acknowledged errno.
static int talk(struct req *r)
{
	int nl = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	if (nl < 0 || send(nl, r, r->h.nlmsg_len, 0) < 0)
		return errno;
	char buf[8192];
	int err = EIO;
	for (int done = 0; !done;) {
		ssize_t n = recv(nl, buf, sizeof buf, 0);
		if (n < 0)
			break;
		for (struct nlmsghdr *h = (void *)buf; NLMSG_OK(h, n); h = NLMSG_NEXT(h, n))
			if (h->nlmsg_type == NLMSG_ERROR) {
				err = -((struct nlmsgerr *)NLMSG_DATA(h))->error;
				done = 1;
			}
	}
	close(nl);
	return err;
}

static int new_veth(const char *name, const char *peer)
{
	struct req r;
	struct ifinfomsg ifi = { .ifi_family = AF_UNSPEC };
	begin(&r, RTM_NEWLINK, NLM_F_CREATE | NLM_F_EXCL, &ifi, sizeof ifi);
	attr(&r, IFLA_IFNAME, name, strlen(name) + 1);
	struct rtattr *info = attr(&r, IFLA_LINKINFO, NULL, 0);
	attr(&r, IFLA_INFO_KIND, "veth", 5);
	struct rtattr *data = attr(&r, IFLA_INFO_DATA, NULL, 0);
	struct rtattr *p = attr(&r, VETH_INFO_PEER, &ifi, sizeof ifi);
	attr(&r, IFLA_IFNAME, peer, strlen(peer) + 1);
	end(&r, p);
	end(&r, data);
	end(&r, info);
	return talk(&r);
}

static int up(int index)
{
	struct req r;
	struct ifinfomsg ifi = { .ifi_family = AF_UNSPEC, .ifi_index = index, .ifi_flags = IFF_UP,
				 .ifi_change = IFF_UP };
	begin(&r, RTM_NEWLINK, 0, &ifi, sizeof ifi);
	return talk(&r);
}

static int add_addr(int family, const char *text, int len, int index)
{
	struct req r;
	unsigned char a[16];
	inet_pton(family, text, a);
	struct ifaddrmsg ifa = { .ifa_family = family, .ifa_prefixlen = len, .ifa_index = index };
	if (family == AF_INET6)
		ifa.ifa_flags = IFA_F_NODAD;
	begin(&r, RTM_NEWADDR, NLM_F_CREATE | NLM_F_EXCL, &ifa, sizeof ifa);
	size_t n = family == AF_INET ? 4 : 16;
	attr(&r, IFA_LOCAL, a, n);
	attr(&r, IFA_ADDRESS, a, n);
	return talk(&r);
}

static struct sockaddr_in sin_of(const char *text, int port)
{
	struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(port) };
	inet_pton(AF_INET, text, &a.sin_addr);
	return a;
}

static struct sockaddr_in6 sin6_of(const char *text, int port)
{
	struct sockaddr_in6 a = { .sin6_family = AF_INET6, .sin6_port = htons(port) };
	inet_pton(AF_INET6, text, &a.sin6_addr);
	return a;
}

// One error queue entry of `fd`, waiting up to 6 s (Linux gives up on a neighbour after 3):
// its extended error, and its offender's address bytes.
static struct sock_extended_err next_error(int fd, int level, unsigned char *offender)
{
	struct pollfd p = { .fd = fd, .events = 0 };
	CHECK(poll(&p, 1, 6000) == 1 && (p.revents & POLLERR));
	char data[64], control[256];
	struct iovec iov = { data, sizeof data };
	struct sockaddr_storage name;
	struct msghdr msg = { .msg_name = &name, .msg_namelen = sizeof name, .msg_iov = &iov,
			      .msg_iovlen = 1, .msg_control = control,
			      .msg_controllen = sizeof control };
	CHECK(recvmsg(fd, &msg, MSG_ERRQUEUE) >= 0);
	struct cmsghdr *c = CMSG_FIRSTHDR(&msg);
	CHECK(c != NULL && c->cmsg_level == level);
	struct sock_extended_err ee;
	memcpy(&ee, CMSG_DATA(c), sizeof ee);
	struct sockaddr_storage off;
	memcpy(&off, (char *)CMSG_DATA(c) + sizeof ee, sizeof(struct sockaddr_in6));
	if (off.ss_family == AF_INET)
		memcpy(offender, &((struct sockaddr_in *)&off)->sin_addr, 4);
	else if (off.ss_family == AF_INET6)
		memcpy(offender, &((struct sockaddr_in6 *)&off)->sin6_addr, 16);
	return ee;
}

static void host_unreachable(void)
{
	int one = 1;
	unsigned char off[16];
	int s = socket(AF_INET, SOCK_DGRAM, 0);
	CHECK(s >= 0 && setsockopt(s, IPPROTO_IP, IP_RECVERR, &one, sizeof one) == 0);
	struct sockaddr_in to = sin_of("10.28.0.9", 4000);
	CHECK(sendto(s, "anyone", 6, 0, (struct sockaddr *)&to, sizeof to) == 6);
	struct sock_extended_err ee = next_error(s, IPPROTO_IP, off);
	CHECK(ee.ee_errno == EHOSTUNREACH && ee.ee_origin == SO_EE_ORIGIN_ICMP);
	CHECK(ee.ee_type == 3 && ee.ee_code == 1);
	struct in_addr own;
	inet_pton(AF_INET, "10.28.0.1", &own);
	CHECK(memcmp(off, &own, 4) == 0);
	close(s);

	int s6 = socket(AF_INET6, SOCK_DGRAM, 0);
	CHECK(s6 >= 0 && setsockopt(s6, IPPROTO_IPV6, IPV6_RECVERR, &one, sizeof one) == 0);
	struct sockaddr_in6 to6 = sin6_of("fd00:28::9", 4000);
	CHECK(sendto(s6, "anyone", 6, 0, (struct sockaddr *)&to6, sizeof to6) == 6);
	ee = next_error(s6, IPPROTO_IPV6, off);
	CHECK(ee.ee_errno == EHOSTUNREACH && ee.ee_origin == SO_EE_ORIGIN_ICMP6);
	CHECK(ee.ee_type == 1 && ee.ee_code == 3);
	close(s6);
}

static void mtu(void)
{
	static char big[2000];
	int one = 1, dont = IP_PMTUDISC_DO;
	unsigned char off[16];
	int s = socket(AF_INET, SOCK_DGRAM, 0);
	CHECK(s >= 0);
	struct sockaddr_in to = sin_of("10.28.0.2", 4000);
	CHECK(sendto(s, big, 1473, 0, (struct sockaddr *)&to, sizeof to) == 1473);
	CHECK(setsockopt(s, IPPROTO_IP, IP_RECVERR, &one, sizeof one) == 0);
	CHECK(setsockopt(s, IPPROTO_IP, IP_MTU_DISCOVER, &dont, sizeof dont) == 0);
	CHECK(sendto(s, big, 1473, 0, (struct sockaddr *)&to, sizeof to) == -1 && errno == EMSGSIZE);
	struct sock_extended_err ee = next_error(s, IPPROTO_IP, off);
	CHECK(ee.ee_errno == EMSGSIZE && ee.ee_origin == SO_EE_ORIGIN_LOCAL && ee.ee_info == 1500);
	CHECK(sendto(s, big, 1472, 0, (struct sockaddr *)&to, sizeof to) == 1472);
	close(s);

	int s6 = socket(AF_INET6, SOCK_DGRAM, 0);
	CHECK(s6 >= 0 && setsockopt(s6, IPPROTO_IPV6, IPV6_RECVERR, &one, sizeof one) == 0);
	struct sockaddr_in6 to6 = sin6_of("fd00:28::2", 4000);
	CHECK(setsockopt(s6, IPPROTO_IPV6, IPV6_DONTFRAG, &one, sizeof one) == 0);
	CHECK(sendto(s6, big, 1453, 0, (struct sockaddr *)&to6, sizeof to6) == -1 &&
	      errno == EMSGSIZE);
	ee = next_error(s6, IPPROTO_IPV6, off);
	CHECK(ee.ee_errno == EMSGSIZE && ee.ee_origin == SO_EE_ORIGIN_LOCAL && ee.ee_info == 1500);
	close(s6);
}

int main(void)
{
	if (geteuid() != 0 || unshare(CLONE_NEWNET) != 0) {
		printf("ok\n");
		return 0;
	}
	CHECK(up(1) == 0);
	CHECK(new_veth("veth0", "veth1") == 0);
	int veth0 = if_nametoindex("veth0");
	CHECK(veth0 > 0);
	CHECK(add_addr(AF_INET, "10.28.0.1", 24, veth0) == 0);
	CHECK(add_addr(AF_INET6, "fd00:28::1", 64, veth0) == 0);
	// veth1 stays down, so veth0 has no carrier: nobody answers neighbour solicitations.
	CHECK(up(veth0) == 0);
	mtu();
	host_unreachable();
	printf("ok\n");
	return 0;
}
