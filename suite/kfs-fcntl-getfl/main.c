// F_GETFL reports the open flags a file keeps (all but O_CREAT, O_EXCL, O_NOCTTY, O_TRUNC and
// O_CLOEXEC), and F_SETFL takes O_DIRECT only for a pipe or a regular file.
//#ExpectedStdout: dir 1 1 cloexec 0
//#ExpectedStdout: file creat 0 trunc 0
//#ExpectedStdout: direct file 0 pipe 0 eventfd -1 EINVAL
//#ExpectedExitCode: 0
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/eventfd.h>
#include <sys/stat.h>
#include <unistd.h>

int main(void)
{
	mkdir("/tmp/d", 0755);
	int dir = open("/tmp/d", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	int fl = fcntl(dir, F_GETFL);
	printf("dir %d %d cloexec %d\n", !!(fl & O_DIRECTORY), !!(fl & O_NOFOLLOW), !!(fl & O_CLOEXEC));
	int file = open("/tmp/f", O_CREAT | O_TRUNC | O_RDWR, 0644);
	fl = fcntl(file, F_GETFL);
	printf("file creat %d trunc %d\n", !!(fl & O_CREAT), !!(fl & O_TRUNC));
	int p[2];
	pipe(p);
	int ev = eventfd(0, 0);
	int a = fcntl(file, F_SETFL, O_DIRECT);
	int b = fcntl(p[0], F_SETFL, O_DIRECT);
	errno = 0;
	int c = fcntl(ev, F_SETFL, O_DIRECT);
	printf("direct file %d pipe %d eventfd %d %s\n", a, b, c, errno == EINVAL ? "EINVAL" : "?");
	return 0;
}
