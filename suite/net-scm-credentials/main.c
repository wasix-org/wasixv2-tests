// Credentials over Unix sockets: SO_PEERCRED on a socketpair, and SCM_CREDENTIALS attached
// by the kernel when the receiver sets SO_PASSCRED.
//#ExpectedStdout: ok
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

#define CHECK(c)                                                                     \
	do {                                                                         \
		if (!(c)) {                                                          \
			printf("FAIL %s:%d: %s (errno=%d %s)\n", __FILE__, __LINE__, \
			       #c, errno, strerror(errno));                          \
			exit(1);                                                     \
		}                                                                    \
	} while (0)

int main(void)
{
	int sv[2];
	CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);

	struct ucred cred;
	socklen_t len = sizeof cred;
	CHECK(getsockopt(sv[0], SOL_SOCKET, SO_PEERCRED, &cred, &len) == 0);
	CHECK(len == sizeof cred);
	CHECK(cred.pid == getpid() && cred.uid == getuid() && cred.gid == getgid());

	int on = 1;
	CHECK(setsockopt(sv[1], SOL_SOCKET, SO_PASSCRED, &on, sizeof on) == 0);
	int v = 0;
	len = sizeof v;
	CHECK(getsockopt(sv[1], SOL_SOCKET, SO_PASSCRED, &v, &len) == 0 && v == 1);

	CHECK(write(sv[0], "c", 1) == 1);
	char data;
	struct iovec iov = { .iov_base = &data, .iov_len = 1 };
	union {
		char buf[CMSG_SPACE(sizeof(struct ucred))];
		struct cmsghdr align;
	} u;
	struct msghdr msg = { .msg_iov = &iov, .msg_iovlen = 1, .msg_control = u.buf,
			      .msg_controllen = sizeof u.buf };
	CHECK(recvmsg(sv[1], &msg, 0) == 1 && data == 'c');
	struct cmsghdr *c = CMSG_FIRSTHDR(&msg);
	CHECK(c != NULL);
	CHECK(c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_CREDENTIALS);
	CHECK(c->cmsg_len == CMSG_LEN(sizeof(struct ucred)));
	struct ucred got;
	memcpy(&got, CMSG_DATA(c), sizeof got);
	CHECK(got.pid == getpid() && got.uid == getuid() && got.gid == getgid());

	// Sending explicit credentials that match our own is allowed.
	struct ucred mine = { .pid = getpid(), .uid = getuid(), .gid = getgid() };
	memset(&u, 0, sizeof u);
	msg.msg_control = u.buf;
	msg.msg_controllen = sizeof u.buf;
	c = CMSG_FIRSTHDR(&msg);
	c->cmsg_level = SOL_SOCKET;
	c->cmsg_type = SCM_CREDENTIALS;
	c->cmsg_len = CMSG_LEN(sizeof mine);
	memcpy(CMSG_DATA(c), &mine, sizeof mine);
	data = 'd';
	CHECK(sendmsg(sv[0], &msg, 0) == 1);
	msg.msg_controllen = sizeof u.buf;
	CHECK(recvmsg(sv[1], &msg, 0) == 1 && data == 'd');
	c = CMSG_FIRSTHDR(&msg);
	CHECK(c && c->cmsg_type == SCM_CREDENTIALS);

	// Someone else's pid is EPERM without privileges.
	mine.pid = getpid() + 1000;
	c = CMSG_FIRSTHDR(&msg);
	c->cmsg_level = SOL_SOCKET;
	c->cmsg_type = SCM_CREDENTIALS;
	c->cmsg_len = CMSG_LEN(sizeof mine);
	memcpy(CMSG_DATA(c), &mine, sizeof mine);
	msg.msg_controllen = sizeof u.buf;
	CHECK(sendmsg(sv[0], &msg, 0) == -1 && errno == EPERM);

	printf("ok\n");
	return 0;
}
