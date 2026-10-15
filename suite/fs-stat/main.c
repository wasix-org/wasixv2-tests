// stat/fstat/lstat/fstatat and statx: types, sizes, link counts, ownership, inode identity,
// AT_SYMLINK_NOFOLLOW, AT_EMPTY_PATH, timestamps and /tmp's mode.
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: CHECK(%s) failed (errno %s)\n", __LINE__, #c, strerror(errno)); fails++; } } while (0)
#define OK(call) do { errno = 0; if ((long)(call) < 0) { fprintf(stderr, "line %d: %s failed: %s\n", __LINE__, #call, strerror(errno)); fails++; } } while (0)
#define FAILS(call, e) do { errno = 0; long r_ = (long)(call); if (r_ != -1 || errno != (e)) { fprintf(stderr, "line %d: %s = %ld (%s), want -1 %s\n", __LINE__, #call, r_, strerror(errno), #e); fails++; } } while (0)

int main(void)
{
	struct stat st, st2;

	OK(stat("/tmp", &st));
	CHECK(S_ISDIR(st.st_mode));
	CHECK((st.st_mode & 07777) == 01777);

	OK(stat("/", &st));
	CHECK(S_ISDIR(st.st_mode));

	int fd = open("/tmp/f", O_CREAT | O_RDWR, 0644);
	CHECK(fd >= 0);
	CHECK(write(fd, "hello", 5) == 5);
	OK(fstat(fd, &st));
	OK(stat("/tmp/f", &st2));
	CHECK(S_ISREG(st.st_mode));
	CHECK(st.st_size == 5);
	CHECK(st.st_nlink == 1);
	CHECK(st.st_uid == getuid() && st.st_gid == getgid());
	CHECK(st.st_blksize > 0);
	CHECK(st.st_ino == st2.st_ino && st.st_dev == st2.st_dev);
	CHECK(st.st_ino != 0);

	/* A write moves mtime and ctime forward. */
	struct timespec before = st.st_mtim;
	struct timespec pause = { 0, 20 * 1000 * 1000 };
	nanosleep(&pause, NULL);
	CHECK(write(fd, "!", 1) == 1);
	OK(fstat(fd, &st));
	CHECK(st.st_mtim.tv_sec > before.tv_sec ||
	      (st.st_mtim.tv_sec == before.tv_sec && st.st_mtim.tv_nsec > before.tv_nsec));
	CHECK(st.st_ctim.tv_sec == st.st_mtim.tv_sec && st.st_ctim.tv_nsec == st.st_mtim.tv_nsec);

	/* Symlinks. */
	OK(symlink("f", "/tmp/l"));
	OK(lstat("/tmp/l", &st));
	CHECK(S_ISLNK(st.st_mode));
	CHECK(st.st_size == 1);
	OK(stat("/tmp/l", &st));
	CHECK(S_ISREG(st.st_mode) && st.st_ino == st2.st_ino);
	int dfd = open("/tmp", O_RDONLY | O_DIRECTORY);
	OK(fstatat(dfd, "l", &st, AT_SYMLINK_NOFOLLOW));
	CHECK(S_ISLNK(st.st_mode));
	OK(fstatat(dfd, "l", &st, 0));
	CHECK(S_ISREG(st.st_mode));
	FAILS(fstatat(dfd, "l", &st, 0x12345), EINVAL);

	/* AT_EMPTY_PATH stats the descriptor itself. */
	OK(fstatat(fd, "", &st, AT_EMPTY_PATH));
	CHECK(st.st_ino == st2.st_ino);
	FAILS(fstatat(fd, "", &st, 0), ENOENT);

	/* Directory link counts. */
	OK(mkdir("/tmp/d", 0755));
	OK(stat("/tmp/d", &st));
	CHECK(st.st_nlink == 2);
	OK(mkdir("/tmp/d/sub", 0755));
	OK(stat("/tmp/d", &st));
	CHECK(st.st_nlink == 3);

	/* statx. */
	struct statx sx;
	OK(statx(AT_FDCWD, "/tmp/f", 0, STATX_BASIC_STATS, &sx));
	CHECK((sx.stx_mask & STATX_BASIC_STATS) == STATX_BASIC_STATS);
	CHECK(S_ISREG(sx.stx_mode));
	CHECK(sx.stx_size == 6);
	CHECK(sx.stx_ino == st2.st_ino);
	CHECK(sx.stx_nlink == 1);
	OK(statx(dfd, "l", AT_SYMLINK_NOFOLLOW, STATX_TYPE, &sx));
	CHECK(S_ISLNK(sx.stx_mode));
	OK(statx(fd, "", AT_EMPTY_PATH, STATX_SIZE, &sx));
	CHECK(sx.stx_size == 6);
	FAILS(statx(AT_FDCWD, "/tmp/f", 0, 0x80000000u, &sx), EINVAL);

	/* Errors. */
	FAILS(stat("/tmp/missing", &st), ENOENT);
	FAILS(stat("/tmp/f/x", &st), ENOTDIR);
	FAILS(stat("", &st), ENOENT);
	FAILS(fstat(999, &st), EBADF);

	/* stdio is not a regular file. */
	OK(fstat(0, &st));
	CHECK(!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode));

	close(fd);
	close(dfd);
	if (fails)
		return 1;
	puts("PASS");
	return 0;
}
