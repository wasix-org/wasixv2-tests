// flock works on pipes and sockets as on any file, through their inode: a pipe's two ends are
// one inode, so their locks conflict, while a socket pair's two sockets are two.
//#ExpectedStdout: pipe 0 -1 EWOULDBLOCK 0
//#ExpectedStdout: socket 0 0
//#ExpectedExitCode: 0
#include <errno.h>
#include <stdio.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <unistd.h>

int main(void)
{
	int p[2], s[2];
	if (pipe(p) || socketpair(AF_UNIX, SOCK_STREAM, 0, s))
		return 1;
	int a = flock(p[0], LOCK_EX);
	errno = 0;
	int b = flock(p[1], LOCK_EX | LOCK_NB);
	int e = errno;
	close(p[0]);
	int c = flock(p[1], LOCK_EX | LOCK_NB);
	printf("pipe %d %d %s %d\n", a, b, e == EWOULDBLOCK ? "EWOULDBLOCK" : "?", c);
	printf("socket %d %d\n", flock(s[0], LOCK_EX | LOCK_NB), flock(s[1], LOCK_EX | LOCK_NB));
	return 0;
}
