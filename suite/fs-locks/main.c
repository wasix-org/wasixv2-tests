// flock, POSIX record locks and OFD locks within one process: OFD locks conflict between
// open file descriptions, POSIX locks of one process never conflict and are dropped by any
// close of the file, flock is separate from both.
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: CHECK(%s) failed (errno %s)\n", __LINE__, #c, strerror(errno)); fails++; } } while (0)
#define OK(call) do { errno = 0; if ((long)(call) < 0) { fprintf(stderr, "line %d: %s failed: %s\n", __LINE__, #call, strerror(errno)); fails++; } } while (0)
#define FAILS(call, e) do { errno = 0; long r_ = (long)(call); if (r_ != -1 || errno != (e)) { fprintf(stderr, "line %d: %s = %ld (%s), want -1 %s\n", __LINE__, #call, r_, strerror(errno), #e); fails++; } } while (0)

static int lk(int fd, int cmd, short type, off_t start, off_t len)
{
	struct flock fl = { .l_type = type, .l_whence = SEEK_SET, .l_start = start, .l_len = len };
	return fcntl(fd, cmd, &fl);
}

int main(void)
{
	int a = open("/tmp/f", O_CREAT | O_RDWR, 0644);
	int b = open("/tmp/f", O_RDWR);

	/* flock. */
	OK(flock(a, LOCK_EX));
	FAILS(flock(b, LOCK_EX | LOCK_NB), EWOULDBLOCK);
	FAILS(flock(b, LOCK_SH | LOCK_NB), EWOULDBLOCK);
	int a2 = dup(a);
	OK(flock(a2, LOCK_EX | LOCK_NB));
	OK(flock(a, LOCK_SH));
	OK(flock(b, LOCK_SH | LOCK_NB));
	OK(flock(b, LOCK_UN));
	OK(flock(a, LOCK_UN));
	OK(flock(b, LOCK_EX | LOCK_NB));
	FAILS(flock(a, 0), EINVAL);
	close(a2);
	/* flock and record locks do not interact. */
	OK(lk(a, F_OFD_SETLK, F_WRLCK, 0, 0));
	OK(lk(a, F_OFD_SETLK, F_UNLCK, 0, 0));
	OK(flock(b, LOCK_UN));

	/* OFD locks conflict between descriptions. */
	OK(lk(a, F_OFD_SETLK, F_WRLCK, 0, 10));
	FAILS(lk(b, F_OFD_SETLK, F_WRLCK, 5, 10), EAGAIN);
	FAILS(lk(b, F_OFD_SETLK, F_RDLCK, 9, 1), EAGAIN);
	OK(lk(b, F_OFD_SETLK, F_WRLCK, 10, 10));
	struct flock q = { .l_type = F_WRLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = 100 };
	OK(fcntl(b, F_OFD_GETLK, &q));
	CHECK(q.l_type == F_WRLCK && q.l_start == 0 && q.l_len == 10 && q.l_pid == -1);
	/* Splitting: unlock the middle. */
	OK(lk(a, F_OFD_SETLK, F_UNLCK, 3, 4));
	OK(lk(b, F_OFD_SETLK, F_WRLCK, 4, 2));
	FAILS(lk(b, F_OFD_SETLK, F_WRLCK, 0, 3), EAGAIN);
	FAILS(lk(b, F_OFD_SETLK, F_WRLCK, 8, 1), EAGAIN);
	struct flock bad = { .l_type = F_WRLCK, .l_pid = 1 };
	FAILS(fcntl(a, F_OFD_SETLK, &bad), EINVAL);
	OK(lk(a, F_OFD_SETLK, F_UNLCK, 0, 0));
	OK(lk(b, F_OFD_SETLK, F_UNLCK, 0, 0));

	/* POSIX locks: one owner per process, no self-conflict. */
	OK(lk(a, F_SETLK, F_WRLCK, 0, 10));
	OK(lk(b, F_SETLK, F_WRLCK, 0, 10));
	struct flock g = { .l_type = F_WRLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = 10 };
	OK(fcntl(a, F_GETLK, &g));
	CHECK(g.l_type == F_UNLCK);
	/* ... but they conflict with OFD locks. */
	int c = open("/tmp/f", O_RDWR);
	FAILS(lk(c, F_OFD_SETLK, F_RDLCK, 0, 1), EAGAIN);
	g.l_type = F_WRLCK;
	OK(fcntl(c, F_OFD_GETLK, &g));
	CHECK(g.l_type == F_WRLCK && g.l_pid == getpid());
	/* Closing any descriptor of the file drops the process's POSIX locks. */
	close(open("/tmp/f", O_RDONLY));
	OK(lk(c, F_OFD_SETLK, F_WRLCK, 0, 10));
	close(c);

	/* Access mode checks. */
	int ro = open("/tmp/f", O_RDONLY);
	FAILS(lk(ro, F_SETLK, F_WRLCK, 0, 1), EBADF);
	FAILS(lk(ro, F_SETLK, 42, 0, 1), EINVAL);
	OK(lk(ro, F_SETLK, F_RDLCK, 0, 1));
	close(ro);

	if (fails)
		return 1;
	puts("PASS");
	return 0;
}
