// /proc/net/icmp, icmp6, raw and raw6 list ping and raw ICMP sockets as ping_v4_seq_show and
// raw_seq_show do: the bound address and identifier (a raw socket's protocol), the peer, the
// state (01 connected, 07 not), the owner's uid and the inode of the socket's file;
// /proc/net/icmp pads its lines to 127 columns. Without CAP_NET_RAW only the headers are
// checked.
//#ExpectedStdout: ok
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(c)                                                                     \
	do {                                                                         \
		if (!(c)) {                                                          \
			printf("FAIL %s:%d: %s (errno=%d %s)\n", __FILE__, __LINE__, \
			       #c, errno, strerror(errno));                          \
			exit(1);                                                     \
		}                                                                    \
	} while (0)

static unsigned long ino_of(int fd)
{
	struct stat st;
	CHECK(fstat(fd, &st) == 0 && S_ISSOCK(st.st_mode));
	return st.st_ino;
}

static void write_sysctl(const char *path, const char *value)
{
	int fd = open(path, O_WRONLY);
	CHECK(fd >= 0);
	CHECK(write(fd, value, strlen(value)) == (ssize_t)strlen(value));
	close(fd);
}

// The row of `file` whose local address is `local`: its remote address, state, uid and
// inode, and the line's length without its newline. 0 if there is none.
struct row {
	char remote[64], state[8];
	unsigned uid;
	unsigned long ino;
	size_t len;
};

static int find(const char *file, const char *header, const char *local, struct row *r)
{
	FILE *f = fopen(file, "r");
	CHECK(f != NULL);
	char line[512];
	CHECK(fgets(line, sizeof line, f) != NULL);
	CHECK(strncmp(line, header, strlen(header)) == 0);
	int found = 0;
	while (!found && fgets(line, sizeof line, f)) {
		char l[64];
		int timeout;
		if (sscanf(line, "%*d: %63s %63s %7s %*s %*s %*s %u %d %lu", l, r->remote, r->state,
			   &r->uid, &timeout, &r->ino) == 6 &&
		    strcmp(l, local) == 0) {
			r->len = strcspn(line, "\n");
			found = 1;
		}
	}
	fclose(f);
	return found;
}

static const char *V4 = "  sl  local_address rem_address   st tx_queue rx_queue tr tm->when "
			"retrnsmt   uid  timeout inode ref pointer drops";
static const char *V6 = "  sl  local_address                         remote_address        "
			"                st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout "
			"inode ref pointer drops";

int main(void)
{
	struct row r;
	int raw = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
	if (raw < 0) {
		CHECK(errno == EPERM);
		for (int i = 0; i < 4; i++) {
			const char *files[] = { "/proc/net/icmp", "/proc/net/raw", "/proc/net/icmp6",
						"/proc/net/raw6" };
			find(files[i], i < 2 ? V4 : V6, "", &r);
		}
		printf("ok\n");
		return 0;
	}
	char range[32];
	snprintf(range, sizeof range, "%u %u", (unsigned)getegid(), (unsigned)getegid());
	write_sysctl("/proc/sys/net/ipv4/ping_group_range", range);

	int ping = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
	CHECK(ping >= 0);
	struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(0x1234) };
	a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	CHECK(bind(ping, (struct sockaddr *)&a, sizeof a) == 0);
	CHECK(connect(ping, (struct sockaddr *)&a, sizeof a) == 0);
	CHECK(find("/proc/net/icmp", V4, "0100007F:1234", &r));
	CHECK(strcmp(r.remote, "0100007F:1234") == 0 && strcmp(r.state, "01") == 0);
	CHECK(r.uid == geteuid() && r.ino == ino_of(ping) && r.len == 127);

	CHECK(find("/proc/net/raw", V4, "00000000:0001", &r));
	CHECK(strcmp(r.remote, "00000000:0000") == 0 && strcmp(r.state, "07") == 0);
	CHECK(r.uid == geteuid() && r.ino == ino_of(raw));

	int raw6 = socket(AF_INET6, SOCK_RAW, IPPROTO_ICMPV6);
	CHECK(raw6 >= 0);
	CHECK(find("/proc/net/raw6", V6, "00000000000000000000000000000000:003A", &r));
	CHECK(strcmp(r.state, "07") == 0 && r.ino == ino_of(raw6));

	int ping6 = socket(AF_INET6, SOCK_DGRAM, IPPROTO_ICMPV6);
	CHECK(ping6 >= 0);
	struct sockaddr_in6 a6 = { .sin6_family = AF_INET6, .sin6_port = htons(0x4321),
				   .sin6_addr = IN6ADDR_LOOPBACK_INIT };
	CHECK(bind(ping6, (struct sockaddr *)&a6, sizeof a6) == 0);
	CHECK(find("/proc/net/icmp6", V6, "00000000000000000000000001000000:4321", &r));
	CHECK(strcmp(r.state, "07") == 0 && r.ino == ino_of(ping6));
	printf("ok\n");
	return 0;
}
