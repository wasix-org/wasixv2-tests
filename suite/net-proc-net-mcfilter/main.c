// /proc/net/mcfilter and /proc/net/mcfilter6 (igmp_mcf_seq_show, igmp6_mcf_seq_show): for each
// source a membership's filter names on loopback, the group, the source and how many
// memberships include and exclude it. Two sockets include 10.0.0.1 for 239.1.2.3 and one also
// 10.0.0.2, which a third (a plain join) blocks; an IPv6 socket includes fd00::1 for ff05::9.
// A plain join of a newer group makes the listing start past loopback, as
// igmp_mcf_get_first only looks at each interface's newest group.
//#ExpectedStdout: ok
#include <arpa/inet.h>
#include <errno.h>
#include <net/if.h>
#include <netinet/in.h>
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

static unsigned lo;

static void source_req(struct group_source_req *r, int family, const char *group, const char *src)
{
	memset(r, 0, sizeof *r);
	r->gsr_interface = lo;
	if (family == AF_INET) {
		struct sockaddr_in *g = (void *)&r->gsr_group, *s = (void *)&r->gsr_source;
		g->sin_family = s->sin_family = AF_INET;
		CHECK(inet_pton(AF_INET, group, &g->sin_addr) == 1);
		CHECK(inet_pton(AF_INET, src, &s->sin_addr) == 1);
	} else {
		struct sockaddr_in6 *g = (void *)&r->gsr_group, *s = (void *)&r->gsr_source;
		g->sin6_family = s->sin6_family = AF_INET6;
		CHECK(inet_pton(AF_INET6, group, &g->sin6_addr) == 1);
		CHECK(inet_pton(AF_INET6, src, &s->sin6_addr) == 1);
	}
}

static void join_source(int fd, int level, int family, const char *group, const char *src)
{
	struct group_source_req r;
	source_req(&r, family, group, src);
	CHECK(setsockopt(fd, level, MCAST_JOIN_SOURCE_GROUP, &r, sizeof r) == 0);
}

static void join(int fd, const char *group)
{
	struct group_req r = { .gr_interface = lo };
	struct sockaddr_in *g = (void *)&r.gr_group;
	g->sin_family = AF_INET;
	CHECK(inet_pton(AF_INET, group, &g->sin_addr) == 1);
	CHECK(setsockopt(fd, IPPROTO_IP, MCAST_JOIN_GROUP, &r, sizeof r) == 0);
}

// The lines of `path` after the header that name loopback.
static void expect(const char *path, const char *header, const char *want)
{
	FILE *f = fopen(path, "r");
	CHECK(f != NULL);
	char line[256], got[1024] = "";
	CHECK(fgets(line, sizeof line, f) != NULL);
	if (strcmp(line, header) != 0) {
		printf("FAIL %s header: %s", path, line);
		exit(1);
	}
	while (fgets(line, sizeof line, f))
		if (strncmp(line + 4, "    lo ", 7) == 0)
			strncat(got, line, sizeof got - strlen(got) - 1);
	fclose(f);
	if (strcmp(got, want) != 0) {
		printf("FAIL %s:\n%swant:\n%s", path, got, want);
		exit(1);
	}
}

int main(void)
{
	const char *head4 = "Idx Device        MCA        SRC    INC    EXC\n";
	const char *head6 = "Idx Device                Multicast Address                   "
			    "Source Address    INC    EXC\n";
	lo = if_nametoindex("lo");
	CHECK(lo != 0);
	int a = socket(AF_INET, SOCK_DGRAM, 0), b = socket(AF_INET, SOCK_DGRAM, 0);
	int c = socket(AF_INET, SOCK_DGRAM, 0), s6 = socket(AF_INET6, SOCK_DGRAM, 0);
	CHECK(a >= 0 && b >= 0 && c >= 0 && s6 >= 0);
	join_source(a, IPPROTO_IP, AF_INET, "239.1.2.3", "10.0.0.1");
	join_source(a, IPPROTO_IP, AF_INET, "239.1.2.3", "10.0.0.2");
	join_source(b, IPPROTO_IP, AF_INET, "239.1.2.3", "10.0.0.1");
	join(c, "239.1.2.3");
	struct group_source_req r;
	source_req(&r, AF_INET, "239.1.2.3", "10.0.0.2");
	CHECK(setsockopt(c, IPPROTO_IP, MCAST_BLOCK_SOURCE, &r, sizeof r) == 0);
	char want[512];
	snprintf(want, sizeof want,
		 "%3u     lo 0xef010203 0x0a000001      2      0\n"
		 "%3u     lo 0xef010203 0x0a000002      1      1\n",
		 lo, lo);
	expect("/proc/net/mcfilter", head4, want);

	join_source(s6, IPPROTO_IPV6, AF_INET6, "ff05::9", "fd00::1");
	snprintf(want, sizeof want,
		 "%3u     lo ff050000000000000000000000000009 fd000000000000000000000000000001"
		 "      1      0\n",
		 lo);
	expect("/proc/net/mcfilter6", head6, want);

	join(c, "239.9.9.9");
	expect("/proc/net/mcfilter", head4, "");
	printf("ok\n");
	return 0;
}
