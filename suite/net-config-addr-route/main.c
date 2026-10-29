// Configuring a network namespace the Linux way, as root in a new one: rtnetlink makes a dummy
// interface (RTM_NEWLINK with IFLA_LINKINFO), gives it IPv4 and IPv6 addresses (RTM_NEWADDR,
// EEXIST for a duplicate), which getifaddrs and /proc/net/if_inet6 show and bind accepts; a
// second address in the subnet is a secondary that goes with its primary (RTM_DELADDR); a
// default route through a gateway on the subnet (RTM_NEWROUTE; ENETUNREACH off it) decides the
// source address an unbound socket connects from, and RTM_GETROUTE finds it; the ioctls
// ifconfig and route use (SIOCSIFADDR, SIOCSIFNETMASK, SIOCADDRT, SIOCDELRT) change the same
// tables, which /proc/net/route shows. Without CAP_NET_ADMIN (a uid other than 0) changes fail
// with EPERM.
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <linux/if_link.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <net/route.h>
#include <netinet/in.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: CHECK(%s) failed (errno %s)\n", __LINE__, #c, strerror(errno)); fails++; } } while (0)
#define FAILS(call, e) do { errno = 0; long r_ = (long)(call); if (r_ != -1 || errno != (e)) { fprintf(stderr, "line %d: %s = %ld (%s), want -1 %s\n", __LINE__, #call, r_, strerror(errno), #e); fails++; } } while (0)

/// A request being built: header, fixed part, attributes.
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

/// Closes a nested attribute opened at `a`.
static void end(struct req *r, struct rtattr *a)
{
	a->rta_len = (char *)tail(r) - (char *)a;
}

/// Sends `r` and returns the acknowledged errno (0 for success), copying any other reply first
/// into `out`.
static int talk(int nl, struct req *r, char *out, size_t outlen)
{
	static unsigned seq;
	r->h.nlmsg_seq = ++seq;
	if (send(nl, r, r->h.nlmsg_len, 0) < 0)
		return errno;
	char buf[8192];
	for (;;) {
		ssize_t n = recv(nl, buf, sizeof buf, 0);
		if (n < 0)
			return errno;
		for (struct nlmsghdr *h = (void *)buf; NLMSG_OK(h, n); h = NLMSG_NEXT(h, n)) {
			if (h->nlmsg_type == NLMSG_ERROR)
				return -((struct nlmsgerr *)NLMSG_DATA(h))->error;
			if (out && h->nlmsg_len <= outlen)
				memcpy(out, h, h->nlmsg_len);
		}
	}
}

static int new_dummy(int nl, const char *name)
{
	struct req r;
	struct ifinfomsg ifi = {.ifi_family = AF_UNSPEC};
	begin(&r, RTM_NEWLINK, NLM_F_CREATE | NLM_F_EXCL, &ifi, sizeof ifi);
	attr(&r, IFLA_IFNAME, name, strlen(name) + 1);
	struct rtattr *info = attr(&r, IFLA_LINKINFO, NULL, 0);
	attr(&r, IFLA_INFO_KIND, "dummy", 6);
	end(&r, info);
	return talk(nl, &r, NULL, 0);
}

static int set_up(int nl, int index)
{
	struct req r;
	struct ifinfomsg ifi = {.ifi_family = AF_UNSPEC, .ifi_index = index, .ifi_flags = IFF_UP, .ifi_change = IFF_UP};
	begin(&r, RTM_NEWLINK, 0, &ifi, sizeof ifi);
	return talk(nl, &r, NULL, 0);
}

/// `ip addr add|del <addr>/<len> dev <index>`.
static int addr(int nl, int type, int family, const char *text, int len, int index)
{
	struct req r;
	unsigned char a[16];
	inet_pton(family, text, a);
	struct ifaddrmsg ifa = {.ifa_family = family, .ifa_prefixlen = len, .ifa_index = index};
	begin(&r, type, type == RTM_NEWADDR ? NLM_F_CREATE | NLM_F_EXCL : 0, &ifa, sizeof ifa);
	size_t n = family == AF_INET ? 4 : 16;
	attr(&r, IFA_LOCAL, a, n);
	attr(&r, IFA_ADDRESS, a, n);
	return talk(nl, &r, NULL, 0);
}

/// `ip route add|del default via <gw>`.
static int default_route(int nl, int type, const char *gw)
{
	struct req r;
	struct in_addr a;
	inet_pton(AF_INET, gw, &a);
	struct rtmsg rtm = {.rtm_family = AF_INET, .rtm_table = RT_TABLE_MAIN};
	if (type == RTM_NEWROUTE) {
		rtm.rtm_protocol = RTPROT_BOOT;
		rtm.rtm_scope = RT_SCOPE_UNIVERSE;
		rtm.rtm_type = RTN_UNICAST;
	} else {
		rtm.rtm_scope = RT_SCOPE_NOWHERE;
	}
	begin(&r, type, type == RTM_NEWROUTE ? NLM_F_CREATE | NLM_F_EXCL : 0, &rtm, sizeof rtm);
	attr(&r, RTA_GATEWAY, &a, 4);
	return talk(nl, &r, NULL, 0);
}

/// The output interface and preferred source of `ip route get <dst>`.
static int route_get(int nl, const char *dst, int *oif, char *src)
{
	struct req r;
	struct in_addr a;
	inet_pton(AF_INET, dst, &a);
	struct rtmsg rtm = {.rtm_family = AF_INET, .rtm_dst_len = 32};
	begin(&r, RTM_GETROUTE, 0, &rtm, sizeof rtm);
	r.h.nlmsg_flags &= ~NLM_F_ACK;
	attr(&r, RTA_DST, &a, 4);
	r.h.nlmsg_seq = 99;
	if (send(nl, &r, r.h.nlmsg_len, 0) < 0)
		return errno;
	char buf[4096];
	ssize_t n = recv(nl, buf, sizeof buf, 0);
	struct nlmsghdr *h = (void *)buf;
	if (n < 0 || !NLMSG_OK(h, n))
		return EIO;
	if (h->nlmsg_type == NLMSG_ERROR)
		return -((struct nlmsgerr *)NLMSG_DATA(h))->error;
	struct rtmsg *m = NLMSG_DATA(h);
	int left = RTM_PAYLOAD(h);
	for (struct rtattr *at = RTM_RTA(m); RTA_OK(at, left); at = RTA_NEXT(at, left)) {
		if (at->rta_type == RTA_OIF)
			memcpy(oif, RTA_DATA(at), 4);
		if (at->rta_type == RTA_PREFSRC)
			inet_ntop(AF_INET, RTA_DATA(at), src, INET_ADDRSTRLEN);
	}
	return 0;
}

/// Whether getifaddrs lists `text` on interface `name`.
static int listed(const char *name, const char *text)
{
	struct ifaddrs *list;
	int found = 0;
	if (getifaddrs(&list))
		return 0;
	for (struct ifaddrs *i = list; i; i = i->ifa_next) {
		char buf[INET6_ADDRSTRLEN];
		if (!i->ifa_addr || strcmp(i->ifa_name, name))
			continue;
		int family = i->ifa_addr->sa_family;
		if (family == AF_INET)
			inet_ntop(family, &((struct sockaddr_in *)i->ifa_addr)->sin_addr, buf, sizeof buf);
		else if (family == AF_INET6)
			inet_ntop(family, &((struct sockaddr_in6 *)i->ifa_addr)->sin6_addr, buf, sizeof buf);
		else
			continue;
		found |= strcmp(buf, text) == 0;
	}
	freeifaddrs(list);
	return found;
}

/// Lines of `path` containing `needle`.
static int lines_with(const char *path, const char *needle)
{
	FILE *f = fopen(path, "r");
	char line[512];
	int n = 0;
	if (!f)
		return -1;
	while (fgets(line, sizeof line, f))
		n += strstr(line, needle) != NULL;
	fclose(f);
	return n;
}

/// The source address a UDP socket connected to `dst` takes.
static void source_for(const char *dst, char *out)
{
	int fd = socket(AF_INET, SOCK_DGRAM, 0);
	struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons(9)};
	socklen_t len = sizeof a;
	inet_pton(AF_INET, dst, &a.sin_addr);
	strcpy(out, "?");
	if (connect(fd, (struct sockaddr *)&a, sizeof a) == 0 && getsockname(fd, (struct sockaddr *)&a, &len) == 0)
		inet_ntop(AF_INET, &a.sin_addr, out, INET_ADDRSTRLEN);
	close(fd);
}

static void sin_of(struct sockaddr *sa, const char *text)
{
	struct sockaddr_in *s = (struct sockaddr_in *)sa;
	memset(s, 0, sizeof *s);
	s->sin_family = AF_INET;
	inet_pton(AF_INET, text, &s->sin_addr);
}

static void configure(void)
{
	int nl = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	CHECK(nl >= 0);
	CHECK(new_dummy(nl, "dummy0") == 0);
	CHECK(new_dummy(nl, "dummy0") == EEXIST);
	int d = if_nametoindex("dummy0");
	CHECK(d > 1);
	CHECK(set_up(nl, d) == 0);
	CHECK(set_up(nl, 1) == 0);

	CHECK(addr(nl, RTM_NEWADDR, AF_INET, "10.11.0.1", 24, d) == 0);
	CHECK(addr(nl, RTM_NEWADDR, AF_INET, "10.11.0.1", 24, d) == EEXIST);
	CHECK(addr(nl, RTM_NEWADDR, AF_INET, "10.11.0.2", 24, d) == 0);
	CHECK(addr(nl, RTM_NEWADDR, AF_INET6, "fd00:11::1", 64, d) == 0);
	CHECK(addr(nl, RTM_NEWADDR, AF_INET, "10.11.0.3", 24, 999) == ENODEV);
	CHECK(listed("dummy0", "10.11.0.1"));
	CHECK(listed("dummy0", "10.11.0.2"));
	CHECK(listed("dummy0", "fd00:11::1"));
	CHECK(lines_with("/proc/net/if_inet6", "fd000011000000000000000000000001") == 1);

	int fd = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in a = {.sin_family = AF_INET};
	inet_pton(AF_INET, "10.11.0.2", &a.sin_addr);
	CHECK(bind(fd, (struct sockaddr *)&a, sizeof a) == 0);
	close(fd);
	fd = socket(AF_INET, SOCK_STREAM, 0);
	inet_pton(AF_INET, "10.11.0.9", &a.sin_addr);
	FAILS(bind(fd, (struct sockaddr *)&a, sizeof a), EADDRNOTAVAIL);
	close(fd);

	CHECK(default_route(nl, RTM_NEWROUTE, "172.16.0.1") == ENETUNREACH);
	CHECK(default_route(nl, RTM_NEWROUTE, "10.11.0.254") == 0);
	CHECK(default_route(nl, RTM_NEWROUTE, "10.11.0.254") == EEXIST);
	char src[INET_ADDRSTRLEN];
	source_for("192.0.2.7", src);
	CHECK(strcmp(src, "10.11.0.1") == 0);
	int oif = 0;
	CHECK(route_get(nl, "192.0.2.7", &oif, src) == 0);
	CHECK(oif == d);
	CHECK(strcmp(src, "10.11.0.1") == 0);
	CHECK(lines_with("/proc/net/route", "dummy0\t00000000\tFE000B0A\t0003") == 1);

	// The primary goes, and its secondary with it (promote_secondaries is 0).
	CHECK(addr(nl, RTM_DELADDR, AF_INET, "10.11.0.1", 24, d) == 0);
	CHECK(addr(nl, RTM_DELADDR, AF_INET, "10.11.0.1", 24, d) == EADDRNOTAVAIL);
	CHECK(!listed("dummy0", "10.11.0.2"));
	CHECK(default_route(nl, RTM_DELROUTE, "10.11.0.254") == ESRCH);

	// ifconfig dummy0 192.168.7.1 netmask 255.255.255.0; route add -net 10.12.0.0/16 dev dummy0.
	int s = socket(AF_INET, SOCK_DGRAM, 0);
	struct ifreq ifr = {0};
	strcpy(ifr.ifr_name, "dummy0");
	sin_of(&ifr.ifr_addr, "192.168.7.1");
	CHECK(ioctl(s, SIOCSIFADDR, &ifr) == 0);
	sin_of(&ifr.ifr_netmask, "255.0.255.0");
	FAILS(ioctl(s, SIOCSIFNETMASK, &ifr), EINVAL);
	sin_of(&ifr.ifr_netmask, "255.255.255.0");
	CHECK(ioctl(s, SIOCSIFNETMASK, &ifr) == 0);
	CHECK(ioctl(s, SIOCGIFBRDADDR, &ifr) == 0);
	CHECK(((struct sockaddr_in *)&ifr.ifr_broadaddr)->sin_addr.s_addr == inet_addr("192.168.7.255"));
	CHECK(listed("dummy0", "192.168.7.1"));
	struct rtentry rt = {0};
	sin_of(&rt.rt_dst, "10.12.0.0");
	sin_of(&rt.rt_genmask, "255.255.0.0");
	rt.rt_flags = RTF_UP;
	rt.rt_dev = "dummy0";
	CHECK(ioctl(s, SIOCADDRT, &rt) == 0);
	CHECK(lines_with("/proc/net/route", "dummy0\t00000C0A\t00000000\t0001") == 1);
	source_for("10.12.3.4", src);
	CHECK(strcmp(src, "192.168.7.1") == 0);
	CHECK(ioctl(s, SIOCDELRT, &rt) == 0);
	FAILS(ioctl(s, SIOCDELRT, &rt), ESRCH);
	close(s);
	close(nl);
}

/// Without CAP_NET_ADMIN every change is EPERM, and reading still works.
static void unprivileged(void)
{
	int nl = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	CHECK(new_dummy(nl, "dummy0") == EPERM);
	CHECK(addr(nl, RTM_NEWADDR, AF_INET, "10.11.0.1", 24, 1) == EPERM);
	int oif = 0;
	char src[INET_ADDRSTRLEN];
	CHECK(route_get(nl, "127.0.0.1", &oif, src) == 0);
	CHECK(oif == 1);
	int s = socket(AF_INET, SOCK_DGRAM, 0);
	struct ifreq ifr = {0};
	strcpy(ifr.ifr_name, "lo");
	sin_of(&ifr.ifr_addr, "10.1.1.1");
	FAILS(ioctl(s, SIOCSIFADDR, &ifr), EPERM);
	close(s);
	close(nl);
}

int main(void)
{
	if (geteuid() != 0)
		unprivileged();
	else if (unshare(CLONE_NEWNET) == 0)
		configure();
	else
		CHECK(!"unshare(CLONE_NEWNET)");
	if (fails)
		return 1;
	puts("PASS");
	return 0;
}
