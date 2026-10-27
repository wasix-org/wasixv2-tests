// One read takes everything a pipe or stream socket holds, up to the buffer, and a datagram
// whole, as Linux's pipe_read and socket reads do: no cap at the kernel's 64 KiB step.
//#ExpectedStdout: pipe 204800
//#ExpectedStdout: stream 204800
//#ExpectedStdout: dgram 102400
//#ExpectedExitCode: 0
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define LEN (200 << 10)

static char out[LEN], in[512 << 10];

/* Writes LEN bytes in as many writes as it takes, then reads once with a larger buffer. */
static ssize_t round_trip(int w, int r, size_t len)
{
	size_t done = 0;
	while (done < len) {
		ssize_t n = write(w, out + done, len - done);
		if (n <= 0)
			return -1;
		done += n;
	}
	return read(r, in, sizeof in);
}

int main(void)
{
	int fds[2];
	memset(out, 'x', sizeof out);
	if (pipe(fds) || fcntl(fds[1], F_SETPIPE_SZ, 1 << 20) < 0)
		return 1;
	printf("pipe %zd\n", round_trip(fds[1], fds[0], LEN));
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds))
		return 1;
	int size = 1 << 20;
	setsockopt(fds[0], SOL_SOCKET, SO_SNDBUF, &size, sizeof size);
	setsockopt(fds[1], SOL_SOCKET, SO_RCVBUF, &size, sizeof size);
	printf("stream %zd\n", round_trip(fds[0], fds[1], LEN));
	if (socketpair(AF_UNIX, SOCK_DGRAM, 0, fds))
		return 1;
	printf("dgram %zd\n", round_trip(fds[0], fds[1], 100 << 10));
	return 0;
}
