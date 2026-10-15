// open(2) flag semantics: access modes, O_CREAT with the umask, O_TRUNC, O_APPEND,
// O_DIRECTORY, O_NOFOLLOW, O_PATH, O_CLOEXEC and the path errors.
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
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
	char buf[64];
	struct stat st;

	umask(022);
	int fd = open("/tmp/f", O_CREAT | O_WRONLY, 0777);
	CHECK(fd >= 0);
	OK(fstat(fd, &st));
	CHECK((st.st_mode & 07777) == 0755);
	CHECK(S_ISREG(st.st_mode));
	CHECK(write(fd, "hello world", 11) == 11);
	FAILS(read(fd, buf, sizeof buf), EBADF);
	CHECK((fcntl(fd, F_GETFL) & O_ACCMODE) == O_WRONLY);
	close(fd);

	fd = open("/tmp/f", O_RDONLY);
	CHECK(fd >= 0);
	FAILS(write(fd, "x", 1), EBADF);
	CHECK(read(fd, buf, sizeof buf) == 11);
	CHECK(fcntl(fd, F_GETFD) == 0);
	close(fd);

	/* O_APPEND: every write goes to the end, whatever the offset. */
	fd = open("/tmp/f", O_RDWR | O_APPEND);
	CHECK(fd >= 0);
	CHECK(fcntl(fd, F_GETFL) & O_APPEND);
	CHECK(lseek(fd, 0, SEEK_SET) == 0);
	CHECK(write(fd, "!", 1) == 1);
	CHECK(lseek(fd, 0, SEEK_CUR) == 12);
	CHECK(pread(fd, buf, sizeof buf, 0) == 12);
	CHECK(memcmp(buf, "hello world!", 12) == 0);
	close(fd);

	/* O_TRUNC empties the file, even with O_RDONLY on Linux. */
	fd = open("/tmp/f", O_RDONLY | O_TRUNC);
	CHECK(fd >= 0);
	OK(fstat(fd, &st));
	CHECK(st.st_size == 0);
	close(fd);

	/* O_CLOEXEC. */
	fd = open("/tmp/f", O_RDONLY | O_CLOEXEC);
	CHECK(fd >= 0);
	CHECK(fcntl(fd, F_GETFD) == FD_CLOEXEC);
	close(fd);

	/* Directories. */
	OK(mkdir("/tmp/d", 0755));
	FAILS(open("/tmp/d", O_WRONLY), EISDIR);
	FAILS(open("/tmp/d", O_RDWR), EISDIR);
	FAILS(open("/tmp/d", O_CREAT | O_RDONLY, 0644), EISDIR);
	FAILS(open("/tmp/f", O_RDONLY | O_DIRECTORY), ENOTDIR);
	FAILS(open("/tmp/new/", O_CREAT | O_RDWR, 0644), EISDIR);
	fd = open("/tmp/d", O_RDONLY | O_DIRECTORY);
	CHECK(fd >= 0);
	FAILS(read(fd, buf, sizeof buf), EISDIR);
	close(fd);

	/* Path errors. */
	FAILS(open("/tmp/missing", O_RDONLY), ENOENT);
	FAILS(open("/tmp/missing/x", O_RDONLY | O_CREAT, 0644), ENOENT);
	FAILS(open("/tmp/f/x", O_RDONLY), ENOTDIR);
	FAILS(open("/tmp/f/", O_RDONLY), ENOTDIR);
	FAILS(open("", O_RDONLY), ENOENT);
	char longname[NAME_MAX + 8];
	memset(longname, 'a', sizeof longname - 1);
	longname[sizeof longname - 1] = 0;
	FAILS(open(longname, O_RDONLY), ENAMETOOLONG);

	/* O_NOFOLLOW on a symlink is ELOOP; with O_PATH it opens the link. */
	OK(symlink("/tmp/f", "/tmp/l"));
	FAILS(open("/tmp/l", O_RDONLY | O_NOFOLLOW), ELOOP);
	fd = open("/tmp/l", O_PATH | O_NOFOLLOW);
	CHECK(fd >= 0);
	OK(fstat(fd, &st));
	CHECK(S_ISLNK(st.st_mode));
	close(fd);

	/* O_PATH descriptors cannot do I/O. */
	fd = open("/tmp/f", O_PATH);
	CHECK(fd >= 0);
	FAILS(read(fd, buf, 1), EBADF);
	FAILS(write(fd, buf, 1), EBADF);
	OK(fstat(fd, &st));
	CHECK(S_ISREG(st.st_mode));
	CHECK((fcntl(fd, F_GETFL) & O_PATH) == O_PATH);
	close(fd);

	/* Unknown flags are ignored by open. */
	fd = open("/tmp/f", O_RDONLY | 0x40000000);
	CHECK(fd >= 0);
	close(fd);

	if (fails)
		return 1;
	puts("PASS");
	return 0;
}
