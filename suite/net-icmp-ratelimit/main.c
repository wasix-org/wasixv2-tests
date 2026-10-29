// The ICMP rate-limit sysctls: icmp_ratelimit (1000 ms), icmp_ratemask (0x1818: destination
// unreachable, source quench, time exceeded, parameter problem), icmp/ratelimit (100 ms in
// current Linux, 1000 in older kernels; either is taken) and icmp/ratemask ("0-1,3-127", as
// proc_do_large_bitmap prints it). As root: once icmp_ratemask names echo replies and
// icmp_msgs_per_sec is 0, the global bucket runs dry and 127.0.0.1 stops answering echo
// requests (icmp_reply checks the global limit even on loopback), and answers again with the
// default mask. Older kernels (6.8) keep icmp_msgs_per_sec global, outside network
// namespaces; there that part is skipped. Not root: only the defaults are read.
//#ExpectedStdout: ok
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/ip_icmp.h>
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

static void read_text(const char *path, char *out, size_t len)
{
	int fd = open(path, O_RDONLY);
	CHECK(fd >= 0);
	ssize_t n = read(fd, out, len - 1);
	CHECK(n >= 0);
	out[n] = 0;
	close(fd);
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

static void expect(const char *path, const char *want)
{
	char got[128];
	read_text(path, got, sizeof got);
	if (strcmp(got, want) != 0) {
		printf("FAIL %s: %s, want %s", path, got, want);
		exit(1);
	}
}

// Whether 127.0.0.1 answers an echo request on a ping socket within `ms`.
static int answered(int p, unsigned short seq, int ms)
{
	struct sockaddr_in to = { .sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
	unsigned char req[8] = { ICMP_ECHO, 0, 0, 0, 0, 0, seq >> 8, seq & 0xff }, buf[64];
	CHECK(sendto(p, req, sizeof req, 0, (struct sockaddr *)&to, sizeof to) == sizeof req);
	struct pollfd pfd = { .fd = p, .events = POLLIN };
	if (poll(&pfd, 1, ms) != 1)
		return 0;
	CHECK(recv(p, buf, sizeof buf, 0) == sizeof req && buf[0] == ICMP_ECHOREPLY);
	return 1;
}

int main(void)
{
	expect("/proc/sys/net/ipv4/icmp_ratelimit", "1000\n");
	expect("/proc/sys/net/ipv4/icmp_ratemask", "6168\n");
	char text[32];
	read_text("/proc/sys/net/ipv6/icmp/ratelimit", text, sizeof text);
	CHECK(strcmp(text, "100\n") == 0 || strcmp(text, "1000\n") == 0);
	expect("/proc/sys/net/ipv6/icmp/ratemask", "0-1,3-127\n");
	if (geteuid() != 0) {
		printf("ok\n");
		return 0;
	}
	const char *mask6 = "/proc/sys/net/ipv6/icmp/ratemask";
	CHECK(write_text(mask6, "0,2-4,255\n") == 0);
	expect(mask6, "0,2-4,255\n");
	CHECK(write_text(mask6, "4-2") == -1 && errno == EINVAL);
	CHECK(write_text(mask6, "256") == -1 && errno == EINVAL);
	CHECK(write_text(mask6, "0-1,3-127") == 0);
	if (access("/proc/sys/net/ipv4/icmp_msgs_per_sec", F_OK) != 0) {
		printf("ok\n");
		return 0;
	}
	CHECK(write_text("/proc/sys/net/ipv4/icmp_msgs_per_sec", "-1") == -1 && errno == EINVAL);

	char range[32];
	snprintf(range, sizeof range, "%u %u", (unsigned)getegid(), (unsigned)getegid());
	CHECK(write_text("/proc/sys/net/ipv4/ping_group_range", range) == 0);
	int p = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
	CHECK(p >= 0);
	CHECK(answered(p, 1, 2000));
	char per_sec[32], burst[32];
	read_text("/proc/sys/net/ipv4/icmp_msgs_per_sec", per_sec, sizeof per_sec);
	read_text("/proc/sys/net/ipv4/icmp_msgs_burst", burst, sizeof burst);
	CHECK(write_text("/proc/sys/net/ipv4/icmp_msgs_burst", "10") == 0);
	CHECK(write_text("/proc/sys/net/ipv4/icmp_msgs_per_sec", "0") == 0);
	CHECK(write_text("/proc/sys/net/ipv4/icmp_ratemask", "1") == 0);
	expect("/proc/sys/net/ipv4/icmp_ratemask", "1\n");
	// Whatever credit the bucket held runs out; nothing refills it.
	int replies = 0;
	for (int i = 0; i < 200; i++)
		replies += answered(p, 2 + i, 20);
	CHECK(replies < 200);
	CHECK(!answered(p, 300, 300));
	CHECK(write_text("/proc/sys/net/ipv4/icmp_ratemask", "6168") == 0);
	CHECK(answered(p, 301, 2000));
	CHECK(write_text("/proc/sys/net/ipv4/icmp_msgs_per_sec", per_sec) == 0);
	CHECK(write_text("/proc/sys/net/ipv4/icmp_msgs_burst", burst) == 0);
	close(p);
	printf("ok\n");
	return 0;
}
