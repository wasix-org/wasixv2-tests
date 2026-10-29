// /proc/sys/net/ipv6/icmp holds IPv6's echo sysctls, 0 by default and any u8 when written
// (proc_dou8vec_minmax without bounds: 256 is EINVAL). As root: with echo_ignore_all set, ::1
// no longer answers an ICMPv6 echo request, while IPv4's icmp_echo_ignore_all stays as it
// was. Not root: only the defaults are read.
//#ExpectedStdout: ok
#include <errno.h>
#include <fcntl.h>
#include <netinet/icmp6.h>
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

static const char *names[] = { "echo_ignore_all", "echo_ignore_anycast", "echo_ignore_multicast" };

static int read_int(const char *path)
{
	FILE *f = fopen(path, "r");
	CHECK(f != NULL);
	int v = -1;
	CHECK(fscanf(f, "%d", &v) == 1);
	fclose(f);
	return v;
}

static int write_text(const char *path, const char *value)
{
	int fd = open(path, O_WRONLY);
	if (fd < 0)
		return -1;
	ssize_t n = write(fd, value, strlen(value));
	int saved = errno;
	close(fd);
	errno = saved;
	return n == (ssize_t)strlen(value) ? 0 : -1;
}

// Whether ::1 answers an echo request on a ping socket within `ms`.
static int answered(int p, int ms)
{
	struct sockaddr_in6 to = { .sin6_family = AF_INET6, .sin6_addr = IN6ADDR_LOOPBACK_INIT };
	unsigned char req[8] = { ICMP6_ECHO_REQUEST, 0, 0, 0, 0, 0, 0, 1 }, buf[64];
	CHECK(sendto(p, req, sizeof req, 0, (struct sockaddr *)&to, sizeof to) == sizeof req);
	struct pollfd pfd = { .fd = p, .events = POLLIN };
	if (poll(&pfd, 1, ms) != 1)
		return 0;
	CHECK(recv(p, buf, sizeof buf, 0) == sizeof req && buf[0] == ICMP6_ECHO_REPLY);
	return 1;
}

int main(void)
{
	char path[128];
	for (int i = 0; i < 3; i++) {
		snprintf(path, sizeof path, "/proc/sys/net/ipv6/icmp/%s", names[i]);
		CHECK(read_int(path) == 0);
	}
	if (geteuid() != 0) {
		printf("ok\n");
		return 0;
	}
	for (int i = 0; i < 3; i++) {
		snprintf(path, sizeof path, "/proc/sys/net/ipv6/icmp/%s", names[i]);
		CHECK(write_text(path, "256") == -1 && errno == EINVAL);
		CHECK(write_text(path, "2") == 0 && read_int(path) == 2);
		CHECK(write_text(path, "0") == 0);
	}
	char range[32];
	snprintf(range, sizeof range, "%u %u", (unsigned)getegid(), (unsigned)getegid());
	CHECK(write_text("/proc/sys/net/ipv4/ping_group_range", range) == 0);
	int p = socket(AF_INET6, SOCK_DGRAM, IPPROTO_ICMPV6);
	CHECK(p >= 0);
	CHECK(answered(p, 2000));
	CHECK(write_text("/proc/sys/net/ipv6/icmp/echo_ignore_all", "1") == 0);
	CHECK(read_int("/proc/sys/net/ipv6/icmp/echo_ignore_all") == 1);
	CHECK(read_int("/proc/sys/net/ipv4/icmp_echo_ignore_all") == 0);
	CHECK(!answered(p, 300));
	CHECK(write_text("/proc/sys/net/ipv6/icmp/echo_ignore_all", "0") == 0);
	CHECK(answered(p, 2000));
	close(p);
	printf("ok\n");
	return 0;
}
