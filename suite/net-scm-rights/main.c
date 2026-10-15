// Passing descriptors over a Unix socket with SCM_RIGHTS: the received descriptor shares the
// open file with the sent one, MSG_CMSG_CLOEXEC applies, and a short control buffer truncates.
//#ExpectedStdout: ok
#include <errno.h>
#include <fcntl.h>
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

static void send_fds(int sock, const int *fds, int n)
{
	char data = 'F';
	struct iovec iov = { .iov_base = &data, .iov_len = 1 };
	union {
		char buf[CMSG_SPACE(sizeof(int) * 4)];
		struct cmsghdr align;
	} u;
	memset(&u, 0, sizeof u);
	struct msghdr msg = { .msg_iov = &iov, .msg_iovlen = 1, .msg_control = u.buf,
			      .msg_controllen = CMSG_SPACE(sizeof(int) * n) };
	struct cmsghdr *c = CMSG_FIRSTHDR(&msg);
	c->cmsg_level = SOL_SOCKET;
	c->cmsg_type = SCM_RIGHTS;
	c->cmsg_len = CMSG_LEN(sizeof(int) * n);
	memcpy(CMSG_DATA(c), fds, sizeof(int) * n);
	CHECK(sendmsg(sock, &msg, 0) == 1);
}

int main(void)
{
	int sv[2];
	CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
	int p[2];
	CHECK(pipe(p) == 0);

	send_fds(sv[0], &p[1], 1);

	char data;
	struct iovec iov = { .iov_base = &data, .iov_len = 1 };
	union {
		char buf[CMSG_SPACE(sizeof(int) * 4)];
		struct cmsghdr align;
	} u;
	struct msghdr msg = { .msg_iov = &iov, .msg_iovlen = 1, .msg_control = u.buf,
			      .msg_controllen = sizeof u.buf };
	CHECK(recvmsg(sv[1], &msg, MSG_CMSG_CLOEXEC) == 1 && data == 'F');
	CHECK(!(msg.msg_flags & MSG_CTRUNC));
	struct cmsghdr *c = CMSG_FIRSTHDR(&msg);
	CHECK(c != NULL && c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS);
	CHECK(c->cmsg_len == CMSG_LEN(sizeof(int)));
	int got;
	memcpy(&got, CMSG_DATA(c), sizeof got);
	CHECK(got >= 0 && got != p[1]);
	CHECK(CMSG_NXTHDR(&msg, c) == NULL);
	CHECK(fcntl(got, F_GETFD) & FD_CLOEXEC);

	// The received descriptor writes into the same pipe.
	CHECK(write(got, "via", 3) == 3);
	char buf[8];
	CHECK(read(p[0], buf, sizeof buf) == 3 && memcmp(buf, "via", 3) == 0);
	// It shares the open file: status flags set through one show through the other.
	CHECK(fcntl(got, F_SETFL, O_NONBLOCK) == 0);
	CHECK(fcntl(p[1], F_GETFL) & O_NONBLOCK);
	close(got);

	// Several descriptors at once, received without MSG_CMSG_CLOEXEC.
	int three[3] = { p[0], p[1], sv[0] };
	send_fds(sv[0], three, 3);
	msg.msg_controllen = sizeof u.buf;
	CHECK(recvmsg(sv[1], &msg, 0) == 1);
	c = CMSG_FIRSTHDR(&msg);
	CHECK(c && c->cmsg_type == SCM_RIGHTS && c->cmsg_len == CMSG_LEN(3 * sizeof(int)));
	int recv3[3];
	memcpy(recv3, CMSG_DATA(c), sizeof recv3);
	for (int i = 0; i < 3; i++) {
		CHECK(recv3[i] >= 0);
		CHECK(!(fcntl(recv3[i], F_GETFD) & FD_CLOEXEC));
		close(recv3[i]);
	}

	// A control buffer with room for one descriptor: the rest are dropped and MSG_CTRUNC set.
	send_fds(sv[0], three, 2);
	union {
		char buf[CMSG_SPACE(sizeof(int))];
		struct cmsghdr align;
	} small;
	msg.msg_control = small.buf;
	msg.msg_controllen = CMSG_LEN(sizeof(int));
	CHECK(recvmsg(sv[1], &msg, 0) == 1);
	CHECK(msg.msg_flags & MSG_CTRUNC);
	c = CMSG_FIRSTHDR(&msg);
	CHECK(c && c->cmsg_type == SCM_RIGHTS && c->cmsg_len == CMSG_LEN(sizeof(int)));
	memcpy(&got, CMSG_DATA(c), sizeof got);
	CHECK(got >= 0);
	close(got);

	// SCM_RIGHTS with a bad descriptor fails the send.
	int bad = 999;
	struct iovec iov2 = { .iov_base = "x", .iov_len = 1 };
	union {
		char buf[CMSG_SPACE(sizeof(int))];
		struct cmsghdr align;
	} b;
	struct msghdr m2 = { .msg_iov = &iov2, .msg_iovlen = 1, .msg_control = b.buf,
			     .msg_controllen = sizeof b.buf };
	c = CMSG_FIRSTHDR(&m2);
	c->cmsg_level = SOL_SOCKET;
	c->cmsg_type = SCM_RIGHTS;
	c->cmsg_len = CMSG_LEN(sizeof(int));
	memcpy(CMSG_DATA(c), &bad, sizeof bad);
	CHECK(sendmsg(sv[0], &m2, 0) == -1 && errno == EBADF);

	printf("ok\n");
	return 0;
}
