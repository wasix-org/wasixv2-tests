// link/linkat, symlink/readlink, unlink and the link counts and loops around them.
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: CHECK(%s) failed (errno %s)\n", __LINE__, #c, strerror(errno)); fails++; } } while (0)
#define OK(call) do { errno = 0; if ((long)(call) < 0) { fprintf(stderr, "line %d: %s failed: %s\n", __LINE__, #call, strerror(errno)); fails++; } } while (0)
#define FAILS(call, e) do { errno = 0; long r_ = (long)(call); if (r_ != -1 || errno != (e)) { fprintf(stderr, "line %d: %s = %ld (%s), want -1 %s\n", __LINE__, #call, r_, strerror(errno), #e); fails++; } } while (0)

int main(void)
{
	struct stat a, b;
	char buf[64];
	int fd = open("/tmp/f", O_CREAT | O_WRONLY, 0644);
	close(fd);

	OK(link("/tmp/f", "/tmp/g"));
	OK(stat("/tmp/f", &a));
	OK(stat("/tmp/g", &b));
	CHECK(a.st_ino == b.st_ino && a.st_nlink == 2);
	FAILS(link("/tmp/f", "/tmp/g"), EEXIST);
	FAILS(link("/tmp/missing", "/tmp/h"), ENOENT);
	OK(mkdir("/tmp/d", 0755));
	FAILS(link("/tmp/d", "/tmp/d2"), EPERM);
	OK(unlink("/tmp/g"));
	OK(stat("/tmp/f", &a));
	CHECK(a.st_nlink == 1);

	/* Symlinks and readlink. */
	OK(symlink("f", "/tmp/s"));
	ssize_t n = readlink("/tmp/s", buf, sizeof buf);
	CHECK(n == 1 && buf[0] == 'f');
	OK(symlink("abcdefgh", "/tmp/long"));
	CHECK(readlink("/tmp/long", buf, 3) == 3 && memcmp(buf, "abc", 3) == 0);
	FAILS(readlink("/tmp/f", buf, sizeof buf), EINVAL);
	FAILS(readlink("/tmp/missing", buf, sizeof buf), ENOENT);
	FAILS(symlink("x", "/tmp/s"), EEXIST);
	FAILS(symlink("", "/tmp/empty"), ENOENT);
	int dfd = open("/tmp", O_RDONLY | O_DIRECTORY);
	CHECK(readlinkat(dfd, "s", buf, sizeof buf) == 1);
	fd = open("/tmp/s", O_PATH | O_NOFOLLOW);
	CHECK(readlinkat(fd, "", buf, sizeof buf) == 1 && buf[0] == 'f');
	close(fd);

	/* Hard links to symlinks: linkat follows only with AT_SYMLINK_FOLLOW. */
	OK(linkat(dfd, "s", dfd, "s2", 0));
	OK(lstat("/tmp/s2", &a));
	CHECK(S_ISLNK(a.st_mode));
	OK(linkat(dfd, "s", dfd, "f2", AT_SYMLINK_FOLLOW));
	OK(lstat("/tmp/f2", &a));
	CHECK(S_ISREG(a.st_mode) && a.st_nlink == 2);
	FAILS(linkat(dfd, "s", dfd, "x", 0x1), EINVAL);

	/* Loops. */
	OK(symlink("loop2", "/tmp/loop1"));
	OK(symlink("loop1", "/tmp/loop2"));
	FAILS(open("/tmp/loop1", O_RDONLY), ELOOP);
	FAILS(stat("/tmp/loop1/x", &a), ELOOP);

	/* Relative targets resolve from the link's directory; symlinked directories. */
	OK(symlink("d", "/tmp/dl"));
	fd = open("/tmp/dl/inner", O_CREAT | O_WRONLY, 0644);
	CHECK(fd >= 0);
	close(fd);
	OK(stat("/tmp/d/inner", &a));
	FAILS(rmdir("/tmp/dl"), ENOTDIR);
	FAILS(unlink("/tmp/d"), EISDIR);
	OK(unlink("/tmp/dl"));
	OK(stat("/tmp/d", &a));

	close(dfd);
	if (fails)
		return 1;
	puts("PASS");
	return 0;
}
