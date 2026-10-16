// fstat on descriptors that are not files reports their pseudo filesystem's inode, as Linux's
// pipefs, sockfs and anon_inodefs do: both ends of a pipe are one FIFO inode and another pipe
// is another; sockets are S_IFSOCK, each its own inode; eventfd and epoll share one anon
// inode with no file type and mode 0600.
//#ExpectedStdout: ok
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;
#define CHECK(cond)                                                                       \
	do {                                                                              \
		if (!(cond)) {                                                            \
			printf("FAIL line %d: %s (errno %d: %s)\n", __LINE__, #cond, errno, \
			       strerror(errno));                                          \
			failures++;                                                       \
		}                                                                         \
	} while (0)

static struct stat st(int fd)
{
	struct stat s;
	memset(&s, 0, sizeof s);
	CHECK(fstat(fd, &s) == 0);
	return s;
}

int main(void)
{
	int p[2], q[2], sv[2];
	CHECK(pipe(p) == 0);
	CHECK(pipe(q) == 0);
	struct stat r = st(p[0]), w = st(p[1]), other = st(q[0]), dup_r = st(dup(p[0]));
	CHECK(S_ISFIFO(r.st_mode));
	CHECK((r.st_mode & 07777) == 0600);
	CHECK(r.st_ino == w.st_ino && r.st_dev == w.st_dev);
	CHECK(r.st_ino == dup_r.st_ino);
	CHECK(r.st_ino != other.st_ino);

	CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
	struct stat a = st(sv[0]), b = st(sv[1]);
	CHECK(S_ISSOCK(a.st_mode) && S_ISSOCK(b.st_mode));
	CHECK(a.st_ino != b.st_ino);
	CHECK(a.st_dev != r.st_dev);

	struct stat e = st(eventfd(0, 0)), ep = st(epoll_create1(0));
	CHECK((e.st_mode & S_IFMT) == 0);
	CHECK((e.st_mode & 07777) == 0600);
	CHECK(e.st_ino == ep.st_ino && e.st_dev == ep.st_dev);
	CHECK(e.st_dev != r.st_dev && e.st_dev != a.st_dev);

	if (!failures) printf("ok\n");
	return failures != 0;
}
