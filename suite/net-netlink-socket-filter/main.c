// Classic BPF filters on a NETLINK_ROUTE socket (net/netlink/af_netlink.c: sk_filter in
// netlink_unicast and netlink_dump). A filter returning 0 drops the replies of an RTM_GETLINK
// dump; one returning 16 cuts each reply to its first message's header; one that keeps only
// what holds an IFLA_IFNAME attribute (SKF_AD_NLATTR from the attributes' start) lets the
// dump through; SO_GET_FILTER reads the program's length back; after SO_DETACH_FILTER whole
// replies come again. Each part uses a socket of its own: a dump whose reply a filter dropped
// is still running there (it goes on at the next recvmsg), and a new one would be EBUSY.
//#ExpectedStdout: ok
#include <errno.h>
#include <linux/filter.h>
#include <linux/if_link.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
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

static void getlink(int nl)
{
	struct {
		struct nlmsghdr h;
		struct ifinfomsg i;
	} req = { .h = { .nlmsg_len = sizeof req, .nlmsg_type = RTM_GETLINK,
			 .nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP, .nlmsg_seq = 1 } };
	CHECK(send(nl, &req, sizeof req, 0) == sizeof req);
}

// The length of the next reply within `ms`, or -1 for none.
static int next(int nl, char *buf, size_t len, int ms)
{
	struct pollfd p = { .fd = nl, .events = POLLIN };
	if (poll(&p, 1, ms) != 1)
		return -1;
	int n = recv(nl, buf, len, MSG_DONTWAIT);
	CHECK(n >= 0);
	return n;
}

static void attach(int nl, struct sock_filter *code, unsigned short len)
{
	struct sock_fprog prog = { .len = len, .filter = code };
	CHECK(setsockopt(nl, SOL_SOCKET, SO_ATTACH_FILTER, &prog, sizeof prog) == 0);
}

static int netlink(void)
{
	int nl = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	CHECK(nl >= 0);
	return nl;
}

int main(void)
{
	static char buf[65536];
	int nl = netlink();
	struct sock_filter drop[] = { BPF_STMT(BPF_RET | BPF_K, 0) };
	attach(nl, drop, 1);
	getlink(nl);
	CHECK(next(nl, buf, sizeof buf, 300) == -1);
	close(nl);

	nl = netlink();
	struct sock_filter header[] = { BPF_STMT(BPF_RET | BPF_K, 16) };
	attach(nl, header, 1);
	getlink(nl);
	CHECK(next(nl, buf, sizeof buf, 2000) == 16);
	struct nlmsghdr *h = (struct nlmsghdr *)buf;
	CHECK(h->nlmsg_type == RTM_NEWLINK && h->nlmsg_len > 16);
	while (next(nl, buf, sizeof buf, 100) >= 0)
		;
	close(nl);

	// A = offset of IFLA_IFNAME among the attributes after the ifinfomsg; 0 drops.
	struct sock_filter named[] = {
		BPF_STMT(BPF_LD | BPF_IMM, NLMSG_HDRLEN + NLMSG_ALIGN(sizeof(struct ifinfomsg))),
		BPF_STMT(BPF_LDX | BPF_IMM, IFLA_IFNAME),
		BPF_STMT(BPF_LD | BPF_W | BPF_ABS, SKF_AD_OFF + SKF_AD_NLATTR),
		BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0, 0, 1),
		BPF_STMT(BPF_RET | BPF_K, 0),
		BPF_STMT(BPF_RET | BPF_K, 0xffff),
	};
	nl = netlink();
	attach(nl, named, 6);
	socklen_t len = 0;
	CHECK(getsockopt(nl, SOL_SOCKET, SO_GET_FILTER, NULL, &len) == 0 && len == 6);
	getlink(nl);
	int n = next(nl, buf, sizeof buf, 2000);
	CHECK(n > 16 && h->nlmsg_type == RTM_NEWLINK);
	while (next(nl, buf, sizeof buf, 100) >= 0)
		;

	int zero = 0;
	CHECK(setsockopt(nl, SOL_SOCKET, SO_DETACH_FILTER, &zero, sizeof zero) == 0);
	CHECK(setsockopt(nl, SOL_SOCKET, SO_DETACH_FILTER, &zero, sizeof zero) == -1 &&
	      errno == ENOENT);
	getlink(nl);
	CHECK(next(nl, buf, sizeof buf, 2000) > 16);
	close(nl);
	printf("ok\n");
	return 0;
}
