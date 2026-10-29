// A veth pair joins two network namespaces, as LTP's network tests and `ip netns` set them up:
// as root in a new namespace, `ip link add veth0 type veth peer name veth1` (RTM_NEWLINK with
// VETH_INFO_PEER; the peer, made first, has the lower index), then veth1 moves into a child's
// new namespace (IFLA_NET_NS_PID), where it arrives down and the parent no longer has it. Each
// side gives its end an address and brings it up; then TCP and UDP cross the pair both ways,
// each side seeing the other's address as the peer's, while neither may bind the other's
// address. Carrier needs both ends up (IFF_RUNNING, /sys/class/net/<if>/carrier of a sysfs the
// child mounts). An address on the link that nobody has is unreachable. Deleting veth0 deletes
// veth1 in the child's namespace too. Not root: skipped.
//#BuildEnv: WASIXCC_WASM_EXCEPTIONS=no
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
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
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%d line %d: CHECK(%s) failed (errno %s)\n", getpid(), __LINE__, #c, strerror(errno)); fails++; } } while (0)

#define PORT 47700

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

/// Sends `r` on a new rtnetlink socket and returns the acknowledged errno.
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
	struct ifinfomsg ifi = {.ifi_family = AF_UNSPEC};
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

/// `ip link set <index> netns <pid>`, as tst_ns_ifmove does it.
static int move_to(int index, pid_t pid)
{
	struct req r;
	struct ifinfomsg ifi = {.ifi_family = AF_UNSPEC, .ifi_index = index, .ifi_change = 0xffffffff};
	begin(&r, RTM_NEWLINK, 0, &ifi, sizeof ifi);
	attr(&r, IFLA_NET_NS_PID, &pid, sizeof pid);
	return talk(&r);
}

static int link_change(int index, int up, int type)
{
	struct req r;
	struct ifinfomsg ifi = {.ifi_family = AF_UNSPEC, .ifi_index = index, .ifi_flags = up ? IFF_UP : 0, .ifi_change = IFF_UP};
	begin(&r, type, 0, &ifi, sizeof ifi);
	return talk(&r);
}

static int add_addr(int family, const char *text, int len, int index)
{
	struct req r;
	unsigned char a[16];
	inet_pton(family, text, a);
	struct ifaddrmsg ifa = {.ifa_family = family, .ifa_prefixlen = len, .ifa_index = index};
	if (family == AF_INET6)
		ifa.ifa_flags = IFA_F_NODAD;
	begin(&r, RTM_NEWADDR, NLM_F_CREATE | NLM_F_EXCL, &ifa, sizeof ifa);
	size_t n = family == AF_INET ? 4 : 16;
	attr(&r, IFA_LOCAL, a, n);
	attr(&r, IFA_ADDRESS, a, n);
	return talk(&r);
}

/// The interface's flags, or -1 if it is not there.
static int flags_of(const char *name)
{
	int s = socket(AF_INET, SOCK_DGRAM, 0);
	struct ifreq ifr = {0};
	strncpy(ifr.ifr_name, name, IFNAMSIZ - 1);
	int r = ioctl(s, SIOCGIFFLAGS, &ifr);
	close(s);
	return r ? -1 : (unsigned short)ifr.ifr_flags;
}

static void configure(int index, const char *v4, const char *v6)
{
	CHECK(add_addr(AF_INET, v4, 24, index) == 0);
	CHECK(add_addr(AF_INET6, v6, 64, index) == 0);
	CHECK(link_change(index, 1, RTM_NEWLINK) == 0);
	CHECK(link_change(1, 1, RTM_NEWLINK) == 0);
}

static void sa_of(struct sockaddr_storage *ss, socklen_t *len, const char *text, int port)
{
	memset(ss, 0, sizeof *ss);
	if (strchr(text, ':')) {
		struct sockaddr_in6 *s = (void *)ss;
		s->sin6_family = AF_INET6;
		s->sin6_port = htons(port);
		inet_pton(AF_INET6, text, &s->sin6_addr);
		*len = sizeof *s;
	} else {
		struct sockaddr_in *s = (void *)ss;
		s->sin_family = AF_INET;
		s->sin_port = htons(port);
		inet_pton(AF_INET, text, &s->sin_addr);
		*len = sizeof *s;
	}
}

static int listener(int type, const char *at)
{
	struct sockaddr_storage ss;
	socklen_t len;
	sa_of(&ss, &len, at, PORT);
	int fd = socket(ss.ss_family, type, 0);
	if (fd < 0 || bind(fd, (struct sockaddr *)&ss, len) || (type == SOCK_STREAM && listen(fd, 4)))
		return -1;
	return fd;
}

/// The address of `ss` as text.
static const char *text_of(struct sockaddr_storage *ss, char *buf)
{
	if (ss->ss_family == AF_INET6)
		return inet_ntop(AF_INET6, &((struct sockaddr_in6 *)ss)->sin6_addr, buf, INET6_ADDRSTRLEN);
	return inet_ntop(AF_INET, &((struct sockaddr_in *)ss)->sin_addr, buf, INET6_ADDRSTRLEN);
}

/// The child's side: listens on both families, answers one TCP connection and one datagram
/// per family, each with the peer address it saw.
static void serve(int tcp4, int tcp6, int udp4, int udp6)
{
	int listeners[] = {tcp4, tcp6};
	for (int i = 0; i < 2; i++) {
		struct sockaddr_storage peer;
		socklen_t len = sizeof peer;
		int c = accept(listeners[i], (struct sockaddr *)&peer, &len);
		CHECK(c >= 0);
		char buf[64], addr[INET6_ADDRSTRLEN];
		ssize_t n = read(c, buf, sizeof buf);
		CHECK(n == 4 && memcmp(buf, "ping", 4) == 0);
		text_of(&peer, addr);
		CHECK(write(c, addr, strlen(addr)) == (ssize_t)strlen(addr));
		close(c);
	}
	int datagrams[] = {udp4, udp6};
	for (int i = 0; i < 2; i++) {
		struct sockaddr_storage peer;
		socklen_t len = sizeof peer;
		char buf[64], addr[INET6_ADDRSTRLEN];
		ssize_t n = recvfrom(datagrams[i], buf, sizeof buf, 0, (struct sockaddr *)&peer, &len);
		CHECK(n == 2);
		text_of(&peer, addr);
		CHECK(sendto(datagrams[i], addr, strlen(addr), 0, (struct sockaddr *)&peer, len) > 0);
	}
}

/// The parent's side of one exchange: what the child saw as the peer address.
static int ask(int type, const char *to, char *seen)
{
	struct sockaddr_storage ss;
	socklen_t len;
	sa_of(&ss, &len, to, PORT);
	int fd = socket(ss.ss_family, type, 0);
	ssize_t n = -1;
	if (type == SOCK_STREAM) {
		if (connect(fd, (struct sockaddr *)&ss, len) == 0 && write(fd, "ping", 4) == 4)
			n = read(fd, seen, INET6_ADDRSTRLEN);
	} else {
		struct timeval tv = {5, 0};
		setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
		if (sendto(fd, "hi", 2, 0, (struct sockaddr *)&ss, len) == 2)
			n = recv(fd, seen, INET6_ADDRSTRLEN, 0);
	}
	close(fd);
	if (n <= 0)
		return -1;
	seen[n] = 0;
	return 0;
}

/// Connecting to an address on the link that nobody has fails with EHOSTUNREACH, at once or
/// after the connection attempt gives up (neighbour resolution, about three seconds).
static void unreachable(const char *to)
{
	struct sockaddr_storage ss;
	socklen_t len;
	sa_of(&ss, &len, to, PORT);
	int fd = socket(ss.ss_family, SOCK_STREAM | SOCK_NONBLOCK, 0);
	int r = connect(fd, (struct sockaddr *)&ss, len);
	int e = errno;
	if (r < 0 && e == EINPROGRESS) {
		struct pollfd p = {.fd = fd, .events = POLLOUT};
		CHECK(poll(&p, 1, 20000) == 1);
		socklen_t elen = sizeof e;
		getsockopt(fd, SOL_SOCKET, SO_ERROR, &e, &elen);
	}
	CHECK(r < 0 && e == EHOSTUNREACH);
	close(fd);
}

static void child(int ready, int go)
{
	char c;
	CHECK(unshare(CLONE_NEWNET | CLONE_NEWNS) == 0);
	CHECK(write(ready, "n", 1) == 1);
	CHECK(read(go, &c, 1) == 1);
	int index = if_nametoindex("veth1");
	CHECK(index > 0);
	CHECK((flags_of("veth1") & IFF_UP) == 0);
	configure(index, "10.23.0.2", "fd00:23::2");
	// veth0 is up already: the carrier is on.
	CHECK(flags_of("veth1") & IFF_RUNNING);
	CHECK(mount("none", "/sys", "sysfs", 0, NULL) == 0);
	char buf[8] = {0};
	FILE *f = fopen("/sys/class/net/veth1/carrier", "r");
	CHECK(f && fgets(buf, sizeof buf, f) && strcmp(buf, "1\n") == 0);
	if (f)
		fclose(f);
	struct stat st;
	CHECK(stat("/sys/class/net/veth0", &st) < 0 && errno == ENOENT);
	int tcp4 = listener(SOCK_STREAM, "10.23.0.2"), tcp6 = listener(SOCK_STREAM, "fd00:23::2");
	int udp4 = listener(SOCK_DGRAM, "10.23.0.2"), udp6 = listener(SOCK_DGRAM, "fd00:23::2");
	CHECK(tcp4 >= 0 && tcp6 >= 0 && udp4 >= 0 && udp6 >= 0);
	CHECK(listener(SOCK_STREAM, "10.23.0.1") < 0 && errno == EADDRNOTAVAIL);
	CHECK(write(ready, "l", 1) == 1);
	serve(tcp4, tcp6, udp4, udp6);
	// Wait for veth0's deletion to take veth1 with it.
	CHECK(read(go, &c, 1) == 1);
	CHECK(flags_of("veth1") == -1);
	exit(fails ? 1 : 0);
}

static void parent(void)
{
	int up[2], down[2];
	char c;
	CHECK(pipe(up) == 0 && pipe(down) == 0);
	CHECK(new_veth("veth0", "veth1") == 0);
	CHECK(new_veth("veth0", "veth1") == EEXIST);
	CHECK(if_nametoindex("veth1") < if_nametoindex("veth0"));
	pid_t pid = fork();
	if (pid == 0)
		child(up[1], down[0]);
	CHECK(read(up[0], &c, 1) == 1);
	CHECK(move_to(if_nametoindex("veth1"), pid) == 0);
	CHECK(if_nametoindex("veth1") == 0);
	int index = if_nametoindex("veth0");
	configure(index, "10.23.0.1", "fd00:23::1");
	CHECK(write(down[1], "g", 1) == 1);
	CHECK(read(up[0], &c, 1) == 1);
	CHECK(flags_of("veth0") & IFF_RUNNING);
	char seen[INET6_ADDRSTRLEN + 1];
	CHECK(ask(SOCK_STREAM, "10.23.0.2", seen) == 0 && strcmp(seen, "10.23.0.1") == 0);
	CHECK(ask(SOCK_STREAM, "fd00:23::2", seen) == 0 && strcmp(seen, "fd00:23::1") == 0);
	CHECK(ask(SOCK_DGRAM, "10.23.0.2", seen) == 0 && strcmp(seen, "10.23.0.1") == 0);
	CHECK(ask(SOCK_DGRAM, "fd00:23::2", seen) == 0 && strcmp(seen, "fd00:23::1") == 0);
	unreachable("10.23.0.9");
	CHECK(link_change(index, 0, RTM_DELLINK) == 0);
	CHECK(if_nametoindex("veth0") == 0);
	CHECK(write(down[1], "d", 1) == 1);
	int status;
	CHECK(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

int main(void)
{
	if (geteuid() != 0) {
		puts("PASS");
		return 0;
	}
	CHECK(unshare(CLONE_NEWNET) == 0);
	parent();
	if (fails)
		return 1;
	puts("PASS");
	return 0;
}
