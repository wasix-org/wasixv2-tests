// sendfile reads from a socket when no offset is given, as Linux's splice from a socket does; an
// offset needs an input with a position (ESPIPE), and a pipe cannot be spliced from (EINVAL).
//#ExpectedStdout: socket 5 hello
//#ExpectedStdout: offset -1 ESPIPE
//#ExpectedStdout: pipe -1 EINVAL
//#ExpectedExitCode: 0
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <unistd.h>

int main(void)
{
	int s[2], p[2];
	char buf[16] = { 0 };
	off_t off = 0;
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, s) || pipe(p))
		return 1;
	int out = open("/tmp/out", O_CREAT | O_RDWR | O_TRUNC, 0644);
	write(s[0], "hello", 5);
	ssize_t n = sendfile(out, s[1], NULL, 5);
	pread(out, buf, sizeof buf - 1, 0);
	printf("socket %zd %s\n", n, buf);
	errno = 0;
	n = sendfile(out, s[1], &off, 5);
	printf("offset %zd %s\n", n, errno == ESPIPE ? "ESPIPE" : strerror(errno));
	errno = 0;
	n = sendfile(out, p[0], NULL, 5);
	printf("pipe %zd %s\n", n, errno == EINVAL ? "EINVAL" : strerror(errno));
	return 0;
}
