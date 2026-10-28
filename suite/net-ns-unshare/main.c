// unshare(CLONE_NEWNET): a new network namespace has a port space of its own, so a port the old
// one listens on is free in it, and abstract Unix names of its own, so the old one's are not
// found; /proc/net (a link to self/net) shows its sockets and its one interface, loopback,
// which starts down (copy_net_ns): 127.0.0.1 is unreachable until SIOCSIFFLAGS brings lo up,
// and RTM_NEWLINK can take it down again. A socket made before keeps its namespace, and setns back
// into the old namespace finds the old port taken. Without CAP_SYS_ADMIN (a uid other than 0),
// unshare fails with EPERM.
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sched.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: CHECK(%s) failed (errno %s)\n", __LINE__, #c, strerror(errno)); fails++; } } while (0)
#define FAILS(call, e) do { errno = 0; long r_ = (long)(call); if (r_ != -1 || errno != (e)) { fprintf(stderr, "line %d: %s = %ld (%s), want -1 %s\n", __LINE__, #call, r_, strerror(errno), #e); fails++; } } while (0)

#define PORT 47613

/// A TCP socket listening on every address at PORT, or -1.
static int listen_tcp(void)
{
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(PORT)};
	if (fd < 0 || bind(fd, (struct sockaddr *)&addr, sizeof addr) || listen(fd, 1)) {
		int e = errno;
		if (fd >= 0)
			close(fd);
		errno = e;
		return -1;
	}
	return fd;
}

/// Whether a TCP connection to 127.0.0.1:PORT is made; errno tells why not.
static int connect_lo(void)
{
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(PORT)};
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	int ok = fd >= 0 && connect(fd, (struct sockaddr *)&addr, sizeof addr) == 0;
	int e = errno;
	if (fd >= 0)
		close(fd);
	errno = e;
	return ok;
}

/// lo's flags, or -1.
static int lo_flags(int fd)
{
	struct ifreq ifr = {.ifr_name = "lo"};
	return ioctl(fd, SIOCGIFFLAGS, &ifr) == 0 ? (unsigned short)ifr.ifr_flags : -1;
}

/// `ip link set lo down` over netlink: RTM_NEWLINK with IFF_UP in ifi_change only. Returns the
/// error of the acknowledgement.
static int lo_down_by_netlink(void)
{
	int nl = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	struct {
		struct nlmsghdr h;
		struct ifinfomsg i;
	} req = {
		.h = {.nlmsg_len = sizeof req, .nlmsg_type = RTM_NEWLINK,
		      .nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK, .nlmsg_seq = 1},
		.i = {.ifi_family = AF_UNSPEC, .ifi_index = (int)if_nametoindex("lo"),
		      .ifi_change = IFF_UP},
	};
	struct {
		struct nlmsghdr h;
		struct nlmsgerr e;
	} ack = {0};
	if (nl < 0 || send(nl, &req, sizeof req, 0) != sizeof req ||
	    recv(nl, &ack, sizeof ack, 0) < (ssize_t)sizeof ack || ack.h.nlmsg_type != NLMSG_ERROR)
		return -1;
	close(nl);
	return ack.e.error;
}

static socklen_t abstract_name(struct sockaddr_un *addr)
{
	static const char name[] = "\0net-ns-unshare";
	memset(addr, 0, sizeof *addr);
	addr->sun_family = AF_UNIX;
	memcpy(addr->sun_path, name, sizeof name - 1);
	return offsetof(struct sockaddr_un, sun_path) + sizeof name - 1;
}

/// The lines of `path` that contain `needle`.
static int lines_with(const char *path, const char *needle)
{
	char buf[8192];
	int fd = open(path, O_RDONLY), count = 0;
	if (fd < 0)
		return -1;
	ssize_t n = read(fd, buf, sizeof buf - 1);
	close(fd);
	buf[n > 0 ? n : 0] = 0;
	for (char *line = strtok(buf, "\n"); line; line = strtok(NULL, "\n"))
		count += strstr(line, needle) != NULL;
	return count;
}

static void in_a_new_namespace(void)
{
	char port[16];
	snprintf(port, sizeof port, ":%04X ", PORT);
	int tcp = listen_tcp();
	CHECK(tcp >= 0);
	struct sockaddr_un addr;
	socklen_t len = abstract_name(&addr);
	int server = socket(AF_UNIX, SOCK_STREAM, 0);
	CHECK(bind(server, (struct sockaddr *)&addr, len) == 0 && listen(server, 1) == 0);
	int early = socket(AF_UNIX, SOCK_STREAM, 0);
	int old = open("/proc/self/ns/net", O_RDONLY);
	CHECK(old >= 0);

	CHECK(unshare(CLONE_NEWNET) == 0);

	int again = listen_tcp();
	CHECK(again >= 0);
	CHECK(lines_with("/proc/net/tcp", port) == 1);
	CHECK(lines_with("/proc/net/dev", ":") == 1);
	CHECK(lines_with("/proc/net/dev", "lo:") == 1);
	int ctl = socket(AF_INET, SOCK_DGRAM, 0);
	CHECK(lo_flags(ctl) == IFF_LOOPBACK);
	CHECK(!connect_lo() && errno == ENETUNREACH);
	struct ifreq up = {.ifr_name = "lo", .ifr_flags = IFF_UP};
	CHECK(ioctl(ctl, SIOCSIFFLAGS, &up) == 0);
	CHECK((lo_flags(ctl) & (IFF_UP | IFF_RUNNING)) == (IFF_UP | IFF_RUNNING));
	CHECK(connect_lo());
	CHECK(lo_down_by_netlink() == 0);
	CHECK(lo_flags(ctl) == IFF_LOOPBACK);
	close(ctl);
	int client = socket(AF_UNIX, SOCK_STREAM, 0);
	FAILS(connect(client, (struct sockaddr *)&addr, len), ECONNREFUSED);
	CHECK(connect(early, (struct sockaddr *)&addr, len) == 0);
	int other = socket(AF_UNIX, SOCK_STREAM, 0);
	CHECK(bind(other, (struct sockaddr *)&addr, len) == 0);

	CHECK(setns(old, CLONE_NEWNET) == 0);

	FAILS(listen_tcp(), EADDRINUSE);
	CHECK(lines_with("/proc/net/tcp", port) == 1);
}

int main(void)
{
	char link[32];
	ssize_t n = readlink("/proc/net", link, sizeof link - 1);
	CHECK(n == 8 && memcmp(link, "self/net", 8) == 0);
	if (geteuid() != 0)
		FAILS(unshare(CLONE_NEWNET), EPERM);
	else
		in_a_new_namespace();
	if (fails)
		return 1;
	puts("PASS");
	return 0;
}
