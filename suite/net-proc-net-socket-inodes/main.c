// /proc/net/{tcp,tcp6,udp,udp6,unix} give each socket the inode of its file (sock_i_ino), the
// one fstat reports and /proc/self/fd names as socket:[ino], and tcp/udp its owner's uid
// (sock_i_uid), so `ss -p` and `netstat -p` find the process holding a socket.
//#ExpectedStdout: ok
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
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
	char path[64], link[64];
	snprintf(path, sizeof path, "/proc/self/fd/%d", fd);
	ssize_t n = readlink(path, link, sizeof link - 1);
	CHECK(n > 0);
	link[n] = 0;
	unsigned long named;
	CHECK(sscanf(link, "socket:[%lu]", &named) == 1 && named == st.st_ino);
	return st.st_ino;
}

static int bound(int family, int type)
{
	int fd = socket(family, type, 0);
	CHECK(fd >= 0);
	struct sockaddr_storage ss = { 0 };
	socklen_t len;
	if (family == AF_INET) {
		struct sockaddr_in *a = (struct sockaddr_in *)&ss;
		a->sin_family = AF_INET;
		a->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		len = sizeof *a;
	} else {
		struct sockaddr_in6 *a = (struct sockaddr_in6 *)&ss;
		a->sin6_family = AF_INET6;
		a->sin6_addr = in6addr_loopback;
		len = sizeof *a;
	}
	CHECK(bind(fd, (struct sockaddr *)&ss, len) == 0);
	if (type == SOCK_STREAM)
		CHECK(listen(fd, 4) == 0);
	return fd;
}

// The uid of the /proc/net/<file> row whose inode is `ino`, or -1 if none has it.
static long row_uid(const char *file, unsigned long ino)
{
	char path[64], line[512];
	snprintf(path, sizeof path, "/proc/net/%s", file);
	FILE *f = fopen(path, "r");
	CHECK(f != NULL);
	CHECK(fgets(line, sizeof line, f) != NULL);
	long found = -1;
	while (fgets(line, sizeof line, f)) {
		unsigned long uid, inode;
		int timeout;
		// sl local rem st tx:rx tr:when retrnsmt uid timeout inode
		CHECK(sscanf(line, "%*s %*s %*s %*s %*s %*s %*s %lu %d %lu", &uid, &timeout,
			     &inode) == 3);
		if (inode == ino)
			found = uid;
	}
	fclose(f);
	return found;
}

static int unix_has(unsigned long ino)
{
	char line[512];
	FILE *f = fopen("/proc/net/unix", "r");
	CHECK(f != NULL);
	CHECK(fgets(line, sizeof line, f) != NULL);
	int found = 0;
	while (fgets(line, sizeof line, f)) {
		unsigned long inode;
		// Num RefCount Protocol Flags Type St Inode [Path]
		CHECK(sscanf(line, "%*s %*s %*s %*s %*s %*s %lu", &inode) == 1);
		found |= inode == ino;
	}
	fclose(f);
	return found;
}

int main(void)
{
	const struct {
		int family, type;
		const char *file;
	} cases[] = {
		{ AF_INET, SOCK_STREAM, "tcp" },
		{ AF_INET6, SOCK_STREAM, "tcp6" },
		{ AF_INET, SOCK_DGRAM, "udp" },
		{ AF_INET6, SOCK_DGRAM, "udp6" },
	};
	for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
		int fd = bound(cases[i].family, cases[i].type);
		CHECK(row_uid(cases[i].file, ino_of(fd)) == (long)geteuid());
		close(fd);
	}

	int pair[2];
	CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
	CHECK(unix_has(ino_of(pair[0])) && unix_has(ino_of(pair[1])));
	int named = socket(AF_UNIX, SOCK_DGRAM, 0);
	struct sockaddr_un sun = { .sun_family = AF_UNIX };
	memcpy(sun.sun_path, "\0wasixv2-inodes", 15);
	CHECK(bind(named, (struct sockaddr *)&sun, sizeof(sa_family_t) + 15) == 0);
	CHECK(unix_has(ino_of(named)));
	printf("ok\n");
	return 0;
}
