// A read into a buffer the kernel cannot write fails with EFAULT and takes nothing from a pipe,
// a FIFO or a stream socket, even when part of the buffer was writable, as Linux's pipe_read
// and unix_stream_read_generic consume a buffer only once all of it was copied out.
//#ExpectedStdout: pipe -1 EFAULT -1 EFAULT abcdef
//#ExpectedStdout: fifo -1 EFAULT -1 EFAULT abcdef
//#ExpectedStdout: unix -1 EFAULT -1 EFAULT -1 EFAULT abcdef
//#ExpectedExitCode: 0
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>

/* Far past the end of linear memory, but inside the 32-bit address space. */
#define BAD ((char *)(uintptr_t)0xf0000000u)

static void result(ssize_t n)
{
	printf(" %zd %s", n, n < 0 && errno == EFAULT ? "EFAULT" : "?");
}

/* After "abcdef" was written to w: a read into a bad buffer, a readv whose second buffer is
 * bad, and for a socket a MSG_WAITALL recv into a bad buffer; then the data, all of it. */
static void check(const char *name, int r, int w, int socket)
{
	if (write(w, "abcdef", 6) != 6) {
		printf("%s write failed\n", name);
		return;
	}
	printf("%s", name);
	errno = 0;
	result(read(r, BAD, 6));
	char head[2], rest[8] = { 0 };
	struct iovec iov[2] = { { head, 2 }, { BAD, 4 } };
	errno = 0;
	result(readv(r, iov, 2));
	if (socket) {
		errno = 0;
		result(recv(r, BAD, 6, MSG_WAITALL));
	}
	ssize_t n = read(r, rest, sizeof rest);
	printf(" %.*s\n", n > 0 ? (int)n : 0, rest);
}

int main(void)
{
	int p[2];
	if (pipe2(p, O_NONBLOCK))
		return 1;
	check("pipe", p[0], p[1], 0);

	if (mkfifo("/tmp/kfs-read-fault-fifo", 0600))
		return 2;
	int fr = open("/tmp/kfs-read-fault-fifo", O_RDONLY | O_NONBLOCK);
	int fw = open("/tmp/kfs-read-fault-fifo", O_WRONLY | O_NONBLOCK);
	if (fr < 0 || fw < 0)
		return 3;
	check("fifo", fr, fw, 0);
	unlink("/tmp/kfs-read-fault-fifo");

	int s[2];
	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, s))
		return 4;
	check("unix", s[1], s[0], 1);
	return 0;
}
