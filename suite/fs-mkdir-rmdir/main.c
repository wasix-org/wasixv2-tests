// mkdir/rmdir/unlink errors, truncate/ftruncate, and unlinking an open file.
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
	char buf[32];

	OK(mkdir("/tmp/d", 0755));
	FAILS(mkdir("/tmp/d", 0755), EEXIST);
	FAILS(mkdir("/tmp/x/y", 0755), ENOENT);
	OK(mkdir("/tmp/d/sub/", 0755));
	int fd = open("/tmp/d/f", O_CREAT | O_RDWR, 0644);
	FAILS(mkdir("/tmp/d/f/g", 0755), ENOTDIR);
	FAILS(rmdir("/tmp/d"), ENOTEMPTY);
	FAILS(rmdir("/tmp/d/f"), ENOTDIR);
	FAILS(rmdir("/tmp/d/."), EINVAL);
	FAILS(rmdir("/tmp/d/sub/.."), ENOTEMPTY);
	FAILS(unlink("/tmp/d/sub"), EISDIR);
	FAILS(unlink("/tmp/d/f/"), ENOTDIR);
	OK(rmdir("/tmp/d/sub/"));
	FAILS(rmdir("/tmp/d/sub"), ENOENT);

	/* truncate / ftruncate. */
	CHECK(write(fd, "hello", 5) == 5);
	OK(ftruncate(fd, 10));
	OK(fstat(fd, &st));
	CHECK(st.st_size == 10);
	CHECK(pread(fd, buf, sizeof buf, 0) == 10 && buf[5] == 0 && buf[9] == 0);
	OK(truncate("/tmp/d/f", 2));
	OK(fstat(fd, &st));
	CHECK(st.st_size == 2);
	FAILS(ftruncate(fd, -1), EINVAL);
	FAILS(truncate("/tmp/d", 0), EISDIR);
	int ro = open("/tmp/d/f", O_RDONLY);
	FAILS(ftruncate(ro, 0), EINVAL);
	close(ro);

	/* Unlinking an open file keeps it usable. */
	OK(unlink("/tmp/d/f"));
	FAILS(stat("/tmp/d/f", &st), ENOENT);
	OK(fstat(fd, &st));
	CHECK(st.st_nlink == 0);
	CHECK(pwrite(fd, "abc", 3, 0) == 3);
	CHECK(pread(fd, buf, sizeof buf, 0) == 3 && memcmp(buf, "abc", 3) == 0);
	close(fd);
	OK(rmdir("/tmp/d"));

	/* Removing a directory that is the cwd. */
	OK(mkdir("/tmp/cwd", 0755));
	OK(chdir("/tmp/cwd"));
	OK(rmdir("/tmp/cwd"));
	FAILS(open("new", O_CREAT | O_WRONLY, 0644), ENOENT);
	FAILS(rmdir("/"), EBUSY);

	if (fails)
		return 1;
	puts("PASS");
	return 0;
}
