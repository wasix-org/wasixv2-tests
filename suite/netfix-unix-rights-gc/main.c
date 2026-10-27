// A Unix socket sent over its own pair and then closed is only kept alive by the message in
// flight: the garbage collector reclaims it when another Unix socket is released, so the files
// in flight never pile up to the RLIMIT_NOFILE that makes SCM_RIGHTS fail with ETOOMANYREFS.
//#ExpectedStdout: ok
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
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

static int send_fd(int sock, int fd)
{
	char data = 'F';
	struct iovec iov = { .iov_base = &data, .iov_len = 1 };
	union {
		char buf[CMSG_SPACE(sizeof(int))];
		struct cmsghdr align;
	} u;
	memset(&u, 0, sizeof u);
	struct msghdr msg = {
		.msg_iov = &iov,
		.msg_iovlen = 1,
		.msg_control = u.buf,
		.msg_controllen = sizeof u.buf,
	};
	struct cmsghdr *c = CMSG_FIRSTHDR(&msg);
	c->cmsg_level = SOL_SOCKET;
	c->cmsg_type = SCM_RIGHTS;
	c->cmsg_len = CMSG_LEN(sizeof(int));
	memcpy(CMSG_DATA(c), &fd, sizeof fd);
	return sendmsg(sock, &msg, 0) == 1 ? 0 : -1;
}

int main(void)
{
	struct rlimit lim;
	CHECK(getrlimit(RLIMIT_NOFILE, &lim) == 0);
	// Twice the limit of cycles, each leaving one file in flight until collected.
	long rounds = (long)lim.rlim_cur * 2;
	for (long i = 0; i < rounds; i++) {
		int sv[2];
		CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
		if (send_fd(sv[0], sv[1]) != 0) {
			printf("FAIL round %ld: errno=%d %s\n", i, errno, strerror(errno));
			return 1;
		}
		close(sv[0]);
		close(sv[1]);
	}
	puts("ok");
	return 0;
}
