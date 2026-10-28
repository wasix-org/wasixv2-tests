// /proc/net/unix lists the Unix sockets of the reader's network namespace as unix_seq_show prints
// them (net/unix/af_unix.c): a listener with __SO_ACCEPTCON, then type, state, the inode of the
// socket's file and the name, '@' standing for the leading NUL of an abstract one; a connected
// client (SS_CONNECTED, no name); the server end of a connection nobody accepted yet, which has
// no file (inode 0, SS_CONNECTING) and the listener's name; a datagram socket another connected
// to, established like its peer, and an unbound unconnected one (SS_UNCONNECTED). A closed socket
// is gone.
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: CHECK(%s) failed (errno %s)\n", __LINE__, #c, strerror(errno)); fails++; } } while (0)

struct entry {
	unsigned flags, type, state;
	unsigned long inode;
	char name[128];
};

static char text[1 << 20];

static void load(void)
{
	int fd = open("/proc/net/unix", O_RDONLY);
	size_t len = 0;
	ssize_t n;
	CHECK(fd >= 0);
	while (fd >= 0 && (n = read(fd, text + len, sizeof text - 1 - len)) > 0)
		len += n;
	close(fd);
	text[len] = 0;
}

/// The entry with `inode`, or with no inode and `name` for inode 0; 0 if there is none.
static int find(unsigned long inode, const char *name, struct entry *out)
{
	char *save, *line = strtok_r(text, "\n", &save);
	int found = 0;
	CHECK(line && strcmp(line, "Num       RefCount Protocol Flags    Type St Inode Path") == 0);
	for (line = strtok_r(NULL, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
		struct entry e = {0};
		unsigned refs, protocol;
		int end = 0;
		if (sscanf(line, "%*x: %x %x %x %x %x %lu%n", &refs, &protocol, &e.flags, &e.type,
			   &e.state, &e.inode, &end) != 6) {
			fprintf(stderr, "bad line: %s\n", line);
			fails++;
			continue;
		}
		if (line[end] == ' ')
			snprintf(e.name, sizeof e.name, "%s", line + end + 1);
		if (e.inode == inode && (inode || strcmp(e.name, name) == 0)) {
			*out = e;
			found++;
		}
		// strtok_r cut the line; put the newline back for the next search.
		line[strlen(line)] = '\n';
	}
	return found;
}

static unsigned long inode_of(int fd)
{
	struct stat st;
	return fstat(fd, &st) == 0 ? st.st_ino : 0;
}

static int expect(int fd, const char *name, unsigned flags, unsigned type, unsigned state)
{
	struct entry e;
	load();
	if (find(fd < 0 ? 0 : inode_of(fd), name, &e) != 1)
		return 0;
	return e.flags == flags && e.type == type && e.state == state && strcmp(e.name, name) == 0;
}

static socklen_t named(struct sockaddr_un *addr, const char *name, int abstract)
{
	memset(addr, 0, sizeof *addr);
	addr->sun_family = AF_UNIX;
	snprintf(addr->sun_path + abstract, sizeof addr->sun_path - 1, "%s", name);
	return offsetof(struct sockaddr_un, sun_path) + abstract + strlen(name) + !abstract;
}

int main(void)
{
	char abstract_name[64], path[64], shown[80];
	snprintf(abstract_name, sizeof abstract_name, "net-proc-net-unix.%d", (int)getpid());
	snprintf(path, sizeof path, "/tmp/net-proc-net-unix.%d", (int)getpid());
	unlink(path);
	struct sockaddr_un addr;
	socklen_t len;

	int abstract_listener = socket(AF_UNIX, SOCK_STREAM, 0);
	len = named(&addr, abstract_name, 1);
	CHECK(bind(abstract_listener, (struct sockaddr *)&addr, len) == 0);
	CHECK(listen(abstract_listener, 4) == 0);
	int path_listener = socket(AF_UNIX, SOCK_STREAM, 0);
	len = named(&addr, path, 0);
	CHECK(bind(path_listener, (struct sockaddr *)&addr, len) == 0);
	CHECK(listen(path_listener, 4) == 0);
	int client = socket(AF_UNIX, SOCK_STREAM, 0);
	CHECK(connect(client, (struct sockaddr *)&addr, len) == 0);

	snprintf(shown, sizeof shown, "@%s", abstract_name);
	CHECK(expect(abstract_listener, shown, 0x10000, 1, 1));
	CHECK(expect(path_listener, path, 0x10000, 1, 1));
	CHECK(expect(client, "", 0, 1, 3));
	CHECK(expect(-1, path, 0, 1, 2));

	int server = socket(AF_UNIX, SOCK_DGRAM, 0), sender = socket(AF_UNIX, SOCK_DGRAM, 0);
	int lone = socket(AF_UNIX, SOCK_SEQPACKET, 0);
	snprintf(shown, sizeof shown, "%s.d", abstract_name);
	len = named(&addr, shown, 1);
	CHECK(bind(server, (struct sockaddr *)&addr, len) == 0);
	CHECK(connect(sender, (struct sockaddr *)&addr, len) == 0);
	snprintf(shown, sizeof shown, "@%s.d", abstract_name);
	CHECK(expect(server, shown, 0, 2, 3));
	CHECK(expect(sender, "", 0, 2, 3));
	CHECK(expect(lone, "", 0, 5, 1));

	unsigned long gone = inode_of(lone);
	struct entry e;
	CHECK(close(lone) == 0);
	load();
	CHECK(find(gone, "", &e) == 0);
	unlink(path);
	if (fails)
		return 1;
	puts("PASS");
	return 0;
}
