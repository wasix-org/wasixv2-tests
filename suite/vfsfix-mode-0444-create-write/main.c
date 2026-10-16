// A file created read-only is writable through the open that created it, and ftruncate and
// O_APPEND need only the descriptor's write access, never the mode bits (git's read-only
// objects, tar x, install -m 0444), on a host directory as on the memory root.
//#MappedDirectory: $temp:/data
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

static int has(const char *path, const char *text)
{
	char buf[64] = { 0 };
	int fd = open(path, O_RDONLY);
	if (fd < 0)
		return 0;
	read(fd, buf, sizeof buf - 1);
	close(fd);
	return !strcmp(buf, text);
}

static void in(const char *dir)
{
	char obj[64], log[64], none[64], data[64];
	struct stat st;
	snprintf(obj, sizeof obj, "%s/obj", dir);
	snprintf(log, sizeof log, "%s/log", dir);
	snprintf(none, sizeof none, "%s/none", dir);
	snprintf(data, sizeof data, "%s/data", dir);

	/* O_CREAT|O_EXCL with 0444, then write and truncate through that descriptor. */
	int fd = open(obj, O_CREAT | O_EXCL | O_RDWR, 0444);
	CHECK(fd >= 0);
	CHECK(write(fd, "object", 6) == 6);
	OK(ftruncate(fd, 3));
	OK(fstat(fd, &st));
	CHECK((st.st_mode & 0777) == 0444 && st.st_size == 3);
	close(fd);
	CHECK(has(obj, "obj"));
	/* Opening it again for writing is refused by its mode. */
	FAILS(open(obj, O_WRONLY), EACCES);

	/* O_APPEND on a file created read-only. */
	fd = open(log, O_CREAT | O_WRONLY | O_APPEND, 0444);
	CHECK(fd >= 0);
	CHECK(write(fd, "a", 1) == 1);
	CHECK(write(fd, "b", 1) == 1);
	CHECK(lseek(fd, 0, SEEK_CUR) == 2);
	close(fd);
	CHECK(has(log, "ab"));

	/* Mode 0: nothing at all is allowed, except through the creating open. */
	fd = open(none, O_CREAT | O_WRONLY, 0);
	CHECK(fd >= 0);
	CHECK(write(fd, "x", 1) == 1);
	close(fd);

	/* A writable descriptor survives fchmod to read-only. */
	fd = open(data, O_CREAT | O_RDWR, 0644);
	CHECK(write(fd, "0123456789", 10) == 10);
	OK(fchmod(fd, 0444));
	OK(ftruncate(fd, 4));
	CHECK(pwrite(fd, "X", 1, 0) == 1);
	close(fd);
	CHECK(has(data, "X123"));

	/* An unnamed read-only file. */
	fd = open(dir, O_TMPFILE | O_RDWR, 0400);
	CHECK(fd >= 0);
	CHECK(write(fd, "tmp", 3) == 3);
	OK(ftruncate(fd, 1));
	close(fd);
}

int main(void)
{
	in("/data");
	in("/tmp");
	if (fails)
		return 1;
	puts("PASS");
	return 0;
}
