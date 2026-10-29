// What `ip netns add`, `ip link set ... netns NAME` and `ip netns exec` do, as root: a child
// unshares a network namespace and bind-mounts /proc/self/ns/net onto a file, which keeps the
// namespace after the child exits; a dummy interface moves into it through IFLA_NET_NS_FD on
// that file, arriving down; RTM_GETNSID finds no id for it until RTM_NEWNSID gives it one
// (EEXIST for a second), and moving an interface into a namespace without one gives it the
// lowest free id; setns through the file enters it, where the dummy is. Not root: skipped.
//#BuildEnv: WASIXCC_WASM_EXCEPTIONS=no
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/if_link.h>
#include <linux/net_namespace.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
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
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: CHECK(%s) failed (errno %s)\n", __LINE__, #c, strerror(errno)); fails++; } } while (0)

struct req {
	struct nlmsghdr h;
	char buf[512];
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
	r->h.nlmsg_flags = NLM_F_REQUEST | flags;
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

/// Sends `r`; returns the acknowledged errno, or for a reply the s32 of its attribute `want`
/// through `value` (and 0).
static int talk(struct req *r, int want, int *value)
{
	int nl = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	if (nl < 0 || send(nl, r, r->h.nlmsg_len, 0) < 0)
		return errno;
	char buf[4096];
	ssize_t n = recv(nl, buf, sizeof buf, 0);
	close(nl);
	struct nlmsghdr *h = (void *)buf;
	if (n < 0 || !NLMSG_OK(h, n))
		return EIO;
	if (h->nlmsg_type == NLMSG_ERROR)
		return -((struct nlmsgerr *)NLMSG_DATA(h))->error;
	int left = h->nlmsg_len - NLMSG_LENGTH(NLMSG_ALIGN(sizeof(struct rtgenmsg)));
	struct rtattr *a = (void *)((char *)NLMSG_DATA(h) + NLMSG_ALIGN(sizeof(struct rtgenmsg)));
	for (; RTA_OK(a, left); a = RTA_NEXT(a, left))
		if (a->rta_type == want)
			memcpy(value, RTA_DATA(a), 4);
	return 0;
}

static int nsid_of(int fd, int *id)
{
	struct req r;
	struct rtgenmsg g = {.rtgen_family = AF_UNSPEC};
	begin(&r, RTM_GETNSID, 0, &g, sizeof g);
	attr(&r, NETNSA_FD, &fd, 4);
	*id = -2;
	return talk(&r, NETNSA_NSID, id);
}

static int set_nsid(int fd, int id)
{
	struct req r;
	struct rtgenmsg g = {.rtgen_family = AF_UNSPEC};
	begin(&r, RTM_NEWNSID, NLM_F_ACK, &g, sizeof g);
	attr(&r, NETNSA_FD, &fd, 4);
	attr(&r, NETNSA_NSID, &id, 4);
	return talk(&r, 0, NULL);
}

static int new_dummy(const char *name)
{
	struct req r;
	struct ifinfomsg ifi = {.ifi_family = AF_UNSPEC, .ifi_flags = IFF_UP, .ifi_change = IFF_UP};
	begin(&r, RTM_NEWLINK, NLM_F_ACK | NLM_F_CREATE | NLM_F_EXCL, &ifi, sizeof ifi);
	attr(&r, IFLA_IFNAME, name, strlen(name) + 1);
	struct rtattr *info = attr(&r, IFLA_LINKINFO, NULL, 0);
	attr(&r, IFLA_INFO_KIND, "dummy", 6);
	info->rta_len = (char *)tail(&r) - (char *)info;
	return talk(&r, 0, NULL);
}

static int move_to_fd(int index, int fd)
{
	struct req r;
	struct ifinfomsg ifi = {.ifi_family = AF_UNSPEC, .ifi_index = index};
	begin(&r, RTM_NEWLINK, NLM_F_ACK, &ifi, sizeof ifi);
	attr(&r, IFLA_NET_NS_FD, &fd, 4);
	return talk(&r, 0, NULL);
}

static int flags_of(const char *name)
{
	int s = socket(AF_INET, SOCK_DGRAM, 0);
	struct ifreq ifr = {0};
	strncpy(ifr.ifr_name, name, IFNAMSIZ - 1);
	int r = ioctl(s, SIOCGIFFLAGS, &ifr);
	close(s);
	return r ? -1 : (unsigned short)ifr.ifr_flags;
}

int main(void)
{
	if (geteuid() != 0) {
		puts("PASS");
		return 0;
	}
	CHECK(unshare(CLONE_NEWNET | CLONE_NEWNS) == 0);
	CHECK(mount("none", "/tmp", "tmpfs", 0, NULL) == 0);
	const char *file = "/tmp/netns-a";
	int f = open(file, O_RDONLY | O_CREAT | O_EXCL, 0);
	CHECK(f >= 0);
	close(f);
	pid_t pid = fork();
	if (pid == 0) {
		if (unshare(CLONE_NEWNET) || mount("/proc/self/ns/net", file, "none", MS_BIND, NULL))
			_exit(1);
		_exit(0);
	}
	int status;
	CHECK(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0);
	int ns = open(file, O_RDONLY);
	CHECK(ns >= 0);

	int id;
	CHECK(nsid_of(ns, &id) == 0 && id == NETNSA_NSID_NOT_ASSIGNED);
	CHECK(set_nsid(ns, 7) == 0);
	CHECK(set_nsid(ns, 8) == EEXIST);
	CHECK(nsid_of(ns, &id) == 0 && id == 7);

	CHECK(new_dummy("dummy0") == 0);
	CHECK(flags_of("dummy0") & IFF_UP);
	CHECK(move_to_fd(if_nametoindex("dummy0"), ns) == 0);
	CHECK(flags_of("dummy0") == -1);
	CHECK(nsid_of(ns, &id) == 0 && id == 7);
	int not_a_netns = open("/proc/self/ns/uts", O_RDONLY);
	CHECK(move_to_fd(1, not_a_netns) == EINVAL);
	close(not_a_netns);
	// Moving an interface names the namespace it went to by an id, the lowest free one.
	// The child keeps its namespace alive until the parent has looked: a namespace nobody
	// holds goes, and its id with it.
	int other = open("/proc/self/ns/net", O_RDONLY);
	int moved[2], done[2];
	char c;
	CHECK(pipe(moved) == 0 && pipe(done) == 0);
	CHECK(new_dummy("dummy1") == 0);
	pid = fork();
	if (pid == 0) {
		if (unshare(CLONE_NEWNET))
			_exit(1);
		int mine = open("/proc/self/ns/net", O_RDONLY);
		int bad = setns(other, CLONE_NEWNET) || move_to_fd(if_nametoindex("dummy1"), mine);
		if (write(moved[1], "m", 1) != 1 || read(done[0], &c, 1) != 1)
			_exit(1);
		_exit(bad);
	}
	CHECK(read(moved[0], &c, 1) == 1);
	CHECK(flags_of("dummy1") == -1);
	struct req r;
	struct rtgenmsg g = {.rtgen_family = AF_UNSPEC};
	begin(&r, RTM_GETNSID, NLM_F_DUMP, &g, sizeof g);
	int first = -2;
	CHECK(talk(&r, NETNSA_NSID, &first) == 0 && first == 0);
	CHECK(write(done[1], "d", 1) == 1);
	CHECK(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0);
	close(other);

	CHECK(setns(ns, CLONE_NEWNET) == 0);
	CHECK(flags_of("dummy0") >= 0 && !(flags_of("dummy0") & IFF_UP));
	CHECK(flags_of("lo") >= 0 && !(flags_of("lo") & IFF_UP));
	close(ns);
	CHECK(umount2(file, MNT_DETACH) == 0);
	if (fails)
		return 1;
	puts("PASS");
	return 0;
}
