// Unix sockets keep the SOL_SOCKET options every family shares and read back what was set;
// SO_REUSEPORT is for IP sockets only. SO_PEERCRED without a peer is pid 0 and the invalid ids
// (cred_to_ucred), a listener reports whoever listened, and SO_PEERGROUPS gives the peer's
// supplementary groups, ERANGE with the needed length for a short buffer.
//#ExpectedStdout: ok
#define _GNU_SOURCE
#include <errno.h>
#include <grp.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
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

static int get_int(int fd, int name)
{
	int v = -7;
	socklen_t len = sizeof v;
	CHECK(getsockopt(fd, SOL_SOCKET, name, &v, &len) == 0 && len == sizeof v);
	return v;
}

int main(void)
{
	int sv[2];
	CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
	struct linger set = { .l_onoff = 1, .l_linger = 5 }, got;
	CHECK(setsockopt(sv[0], SOL_SOCKET, SO_LINGER, &set, sizeof set) == 0);
	socklen_t len = sizeof got;
	CHECK(getsockopt(sv[0], SOL_SOCKET, SO_LINGER, &got, &len) == 0);
	CHECK(len == sizeof got && got.l_onoff == 1 && got.l_linger == 5);
	int v = 10;
	CHECK(setsockopt(sv[0], SOL_SOCKET, SO_RCVLOWAT, &v, sizeof v) == 0);
	CHECK(get_int(sv[0], SO_RCVLOWAT) == 10);
	v = 1;
	CHECK(setsockopt(sv[0], SOL_SOCKET, SO_KEEPALIVE, &v, sizeof v) == 0);
	CHECK(get_int(sv[0], SO_KEEPALIVE) == 1);
	CHECK(setsockopt(sv[0], SOL_SOCKET, SO_OOBINLINE, &v, sizeof v) == 0);
	CHECK(get_int(sv[0], SO_OOBINLINE) == 1);
	CHECK(get_int(sv[0], SO_PRIORITY) == 0);
	CHECK(setsockopt(sv[0], SOL_SOCKET, SO_REUSEPORT, &v, sizeof v) == -1 &&
	      errno == EOPNOTSUPP);
	CHECK(get_int(sv[0], SO_PROTOCOL) == 0);

	// The pair's peer is this process.
	struct ucred cred;
	len = sizeof cred;
	CHECK(getsockopt(sv[0], SOL_SOCKET, SO_PEERCRED, &cred, &len) == 0);
	CHECK(cred.pid == getpid() && cred.uid == geteuid() && cred.gid == getegid());
	gid_t groups[64];
	int n = getgroups(64, groups);
	CHECK(n >= 0);
	gid_t peer[64];
	len = 0;
	CHECK(getsockopt(sv[0], SOL_SOCKET, SO_PEERGROUPS, peer, &len) == -1 || n == 0);
	if (n > 0)
		CHECK(errno == ERANGE && len == n * sizeof(gid_t));
	len = sizeof peer;
	CHECK(getsockopt(sv[0], SOL_SOCKET, SO_PEERGROUPS, peer, &len) == 0);
	CHECK(len == n * sizeof(gid_t) && memcmp(peer, groups, len) == 0);

	// No peer: pid 0, uid and gid -1, no groups.
	int lone = socket(AF_UNIX, SOCK_STREAM, 0);
	CHECK(lone >= 0);
	len = sizeof cred;
	CHECK(getsockopt(lone, SOL_SOCKET, SO_PEERCRED, &cred, &len) == 0);
	CHECK(cred.pid == 0 && cred.uid == (uid_t)-1 && cred.gid == (gid_t)-1);
	len = sizeof peer;
	CHECK(getsockopt(lone, SOL_SOCKET, SO_PEERGROUPS, peer, &len) == -1 && errno == ENODATA);

	// A listener reports itself, as unix_listen sets its peer credentials.
	struct sockaddr_un addr = { .sun_family = AF_UNIX };
	memcpy(addr.sun_path, "\0netfix-sockopts", 16);
	socklen_t alen = offsetof(struct sockaddr_un, sun_path) + 16;
	CHECK(bind(lone, (struct sockaddr *)&addr, alen) == 0);
	CHECK(listen(lone, 1) == 0);
	len = sizeof cred;
	CHECK(getsockopt(lone, SOL_SOCKET, SO_PEERCRED, &cred, &len) == 0);
	CHECK(cred.pid == getpid());
	puts("ok");
	return 0;
}
