// connect on a netlink socket checks its address as netlink_connect does: AF_UNSPEC
// disconnects, another family or a short address is EINVAL, and pointing an rtnetlink socket
// at another process or a multicast group needs CAP_NET_ADMIN. getpeername reports where it
// points; shutdown is not supported (sock_no_shutdown).
//#ExpectedStdout: ok
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
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

// <linux/netlink.h>, which the sysroot does not ship.
struct sockaddr_nl {
	sa_family_t nl_family;
	unsigned short nl_pad;
	uint32_t nl_pid;
	uint32_t nl_groups;
};
#define NETLINK_ROUTE 0
#define RTMGRP_LINK 1

int main(void)
{
	int fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	CHECK(fd >= 0);
	struct sockaddr_nl nl = { .nl_family = AF_NETLINK };
	CHECK(connect(fd, (struct sockaddr *)&nl, sizeof nl) == 0);
	CHECK(connect(fd, (struct sockaddr *)&nl, 8) == -1 && errno == EINVAL);
	struct sockaddr_in in = { .sin_family = AF_INET };
	CHECK(connect(fd, (struct sockaddr *)&in, sizeof in) == -1 && errno == EINVAL);
	nl.nl_pid = 4242;
	CHECK(connect(fd, (struct sockaddr *)&nl, sizeof nl) == -1 && errno == EPERM);
	nl.nl_pid = 0;
	nl.nl_groups = RTMGRP_LINK;
	CHECK(connect(fd, (struct sockaddr *)&nl, sizeof nl) == -1 && errno == EPERM);
	struct sockaddr unspec = { .sa_family = AF_UNSPEC };
	CHECK(connect(fd, &unspec, sizeof unspec) == 0);
	struct sockaddr_nl peer;
	socklen_t len = sizeof peer;
	CHECK(getpeername(fd, (struct sockaddr *)&peer, &len) == 0);
	CHECK(peer.nl_family == AF_NETLINK && peer.nl_pid == 0 && peer.nl_groups == 0);
	CHECK(shutdown(fd, SHUT_RDWR) == -1 && errno == EOPNOTSUPP);
	close(fd);
	puts("ok");
	return 0;
}
