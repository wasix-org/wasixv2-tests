// O_CREAT|O_EXCL: fails on any existing name, including dangling symlinks; plain O_CREAT
// follows a dangling symlink and creates its target.
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
	struct stat st;

	int fd = open("/tmp/a", O_CREAT | O_EXCL | O_WRONLY, 0600);
	CHECK(fd >= 0);
	close(fd);
	FAILS(open("/tmp/a", O_CREAT | O_EXCL | O_WRONLY, 0600), EEXIST);
	fd = open("/tmp/a", O_CREAT | O_WRONLY, 0600);
	CHECK(fd >= 0);
	close(fd);

	OK(mkdir("/tmp/dir", 0700));
	FAILS(open("/tmp/dir", O_CREAT | O_EXCL | O_RDONLY, 0600), EEXIST);

	/* A dangling symlink: O_EXCL never follows it. */
	OK(symlink("/tmp/target", "/tmp/dangling"));
	FAILS(open("/tmp/dangling", O_CREAT | O_EXCL | O_WRONLY, 0600), EEXIST);
	CHECK(lstat("/tmp/target", &st) == -1 && errno == ENOENT);

	/* Without O_EXCL the target is created. */
	fd = open("/tmp/dangling", O_CREAT | O_WRONLY, 0640);
	CHECK(fd >= 0);
	close(fd);
	OK(lstat("/tmp/target", &st));
	CHECK(S_ISREG(st.st_mode));
	CHECK((st.st_mode & 0777) == (0640 & ~022));

	/* ... but not through O_NOFOLLOW. */
	OK(symlink("/tmp/target2", "/tmp/dangling2"));
	FAILS(open("/tmp/dangling2", O_CREAT | O_WRONLY | O_NOFOLLOW, 0600), ELOOP);

	/* creat() is O_CREAT|O_WRONLY|O_TRUNC. */
	fd = open("/tmp/c", O_CREAT | O_WRONLY, 0644);
	CHECK(write(fd, "abc", 3) == 3);
	close(fd);
	fd = creat("/tmp/c", 0644);
	CHECK(fd >= 0);
	OK(fstat(fd, &st));
	CHECK(st.st_size == 0);
	CHECK((fcntl(fd, F_GETFL) & O_ACCMODE) == O_WRONLY);
	close(fd);

	/* O_EXCL without O_CREAT is ignored for regular files. */
	fd = open("/tmp/c", O_RDONLY | O_EXCL);
	CHECK(fd >= 0);
	close(fd);

	/* Creating in a directory without write permission. */
	OK(mkdir("/tmp/ro", 0555));
	FAILS(open("/tmp/ro/x", O_CREAT | O_WRONLY, 0600), EACCES);
	/* An existing name wins over the permission error with O_EXCL? No: EACCES first only
	 * when the name is missing; an existing name is EEXIST. */
	OK(chmod("/tmp/ro", 0755));
	fd = open("/tmp/ro/x", O_CREAT | O_WRONLY, 0600);
	CHECK(fd >= 0);
	close(fd);
	OK(chmod("/tmp/ro", 0555));
	FAILS(open("/tmp/ro/x", O_CREAT | O_EXCL | O_WRONLY, 0600), EEXIST);

	if (fails)
		return 1;
	puts("PASS");
	return 0;
}
