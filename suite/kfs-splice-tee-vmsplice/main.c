// splice between files, pipes and a socket with and without offsets, tee duplicating pipe
// data without consuming it, and vmsplice into and out of a pipe, with fs/splice.c's errors.
//#ExpectedStdout: file>pipe 5 off 11 pos 0
//#ExpectedStdout: pipe>file 5 off 7 pos 0 [xxhello]
//#ExpectedStdout: tee 3 abc abcdef
//#ExpectedStdout: pipe>pipe 4 abcd ef
//#ExpectedStdout: socket 4 4 ping
//#ExpectedStdout: vmsplice 11 11 hello world
//#ExpectedStdout: errors ESPIPE EINVAL EINVAL EINVAL EBADF EAGAIN EAGAIN
//#ExpectedExitCode: 0
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

static const char *name(ssize_t ret)
{
	if (ret >= 0)
		return "ok";
	switch (errno) {
	case ESPIPE: return "ESPIPE";
	case EINVAL: return "EINVAL";
	case EBADF: return "EBADF";
	case EAGAIN: return "EAGAIN";
	default: return strerror(errno);
	}
}

static int drain(int fd, char *buf, size_t len)
{
	ssize_t n = read(fd, buf, len - 1);
	buf[n > 0 ? n : 0] = 0;
	return (int)n;
}

int main(void)
{
	char buf[64];
	int in = open("/tmp/splice-in", O_CREAT | O_RDWR | O_TRUNC, 0600);
	int out = open("/tmp/splice-out", O_CREAT | O_RDWR | O_TRUNC, 0600);
	int a[2], b[2];
	if (in < 0 || out < 0 || pipe(a) || pipe2(b, O_NONBLOCK))
		return 1;
	if (write(in, "hello world", 11) != 11 || lseek(in, 0, SEEK_SET) != 0)
		return 2;

	/* A file into a pipe at an offset: the offset moves, the position does not. */
	loff_t off = 6;
	ssize_t n = splice(in, &off, a[1], NULL, 100, 0);
	printf("file>pipe %zd off %lld pos %lld\n", n, (long long)off,
	       (long long)lseek(in, 0, SEEK_CUR));
	drain(a[0], buf, sizeof buf);

	/* A pipe into a file at an offset. */
	write(a[1], "hello", 5);
	write(out, "xx", 2);
	lseek(out, 0, SEEK_SET);
	off = 2;
	n = splice(a[0], NULL, out, &off, 100, SPLICE_F_MOVE);
	printf("pipe>file %zd off %lld pos %lld [", n, (long long)off,
	       (long long)lseek(out, 0, SEEK_CUR));
	drain(out, buf, sizeof buf);
	printf("%s]\n", buf);

	/* tee copies, the input keeps its data. */
	write(a[1], "abcdef", 6);
	n = tee(a[0], b[1], 3, 0);
	drain(b[0], buf, sizeof buf);
	printf("tee %zd %s", n, buf);
	char rest[16];
	ssize_t len = read(a[0], rest, sizeof rest);
	printf(" %.*s\n", (int)len, rest);

	/* splice moves between pipes. */
	write(a[1], "abcdef", 6);
	n = splice(a[0], NULL, b[1], NULL, 4, 0);
	drain(b[0], buf, sizeof buf);
	printf("pipe>pipe %zd %s", n, buf);
	drain(a[0], buf, sizeof buf);
	printf(" %s\n", buf);

	/* A socket into a pipe and the pipe back into the socket. */
	int s[2];
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, s))
		return 3;
	write(s[0], "ping", 4);
	ssize_t got = splice(s[1], NULL, a[1], NULL, 100, 0);
	ssize_t put = splice(a[0], NULL, s[1], NULL, 100, SPLICE_F_MORE);
	drain(s[0], buf, sizeof buf);
	printf("socket %zd %zd %s\n", got, put, buf);

	/* vmsplice into the pipe and back out. */
	struct iovec iov[2] = { { "hello", 5 }, { " world", 6 } };
	ssize_t in_n = vmsplice(a[1], iov, 2, 0);
	char x[5], y[7] = { 0 };
	struct iovec back[2] = { { x, 5 }, { y, 6 } };
	ssize_t out_n = vmsplice(a[0], back, 2, 0);
	printf("vmsplice %zd %zd %.5s%s\n", in_n, out_n, x, y);

	/* The refusals. */
	printf("errors");
	off = 0;
	printf(" %s", name(splice(a[0], &off, out, NULL, 1, 0)));   /* pipe with an offset */
	printf(" %s", name(splice(in, NULL, out, NULL, 1, 0)));     /* no pipe */
	printf(" %s", name(splice(a[0], NULL, a[1], NULL, 1, 0)));  /* the same pipe */
	printf(" %s", name(tee(a[0], a[1], 1, 0)));                 /* tee to itself */
	printf(" %s", name(tee(a[1], b[1], 1, 0)));                 /* tee from a write end */
	printf(" %s", name(splice(a[0], NULL, b[1], NULL, 1, SPLICE_F_NONBLOCK)));
	printf(" %s\n", name(vmsplice(a[0], back, 2, SPLICE_F_NONBLOCK)));

	unlink("/tmp/splice-in");
	unlink("/tmp/splice-out");
	return 0;
}
