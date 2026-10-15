// utimensat/futimens, fallocate, fsync, statfs/fstatfs/statvfs.
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <unistd.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: CHECK(%s) failed (errno %s)\n", __LINE__, #c, strerror(errno)); fails++; } } while (0)
#define OK(call) do { errno = 0; if ((long)(call) < 0) { fprintf(stderr, "line %d: %s failed: %s\n", __LINE__, #call, strerror(errno)); fails++; } } while (0)
#define FAILS(call, e) do { errno = 0; long r_ = (long)(call); if (r_ != -1 || errno != (e)) { fprintf(stderr, "line %d: %s = %ld (%s), want -1 %s\n", __LINE__, #call, r_, strerror(errno), #e); fails++; } } while (0)

int main(void)
{
	struct stat st;
	char buf[64];
	int fd = open("/tmp/f", O_CREAT | O_RDWR, 0644);

	/* utimensat. */
	struct timespec ts[2] = { { 1000, 123456789 }, { 2000, 987654321 } };
	OK(utimensat(AT_FDCWD, "/tmp/f", ts, 0));
	OK(stat("/tmp/f", &st));
	CHECK(st.st_atim.tv_sec == 1000 && st.st_atim.tv_nsec == 123456789);
	CHECK(st.st_mtim.tv_sec == 2000 && st.st_mtim.tv_nsec == 987654321);
	struct timespec omit[2] = { { 5, UTIME_OMIT }, { 3000, 0 } };
	OK(futimens(fd, omit));
	OK(fstat(fd, &st));
	CHECK(st.st_atim.tv_sec == 1000 && st.st_mtim.tv_sec == 3000);
	struct timespec now[2] = { { 0, UTIME_NOW }, { 0, UTIME_OMIT } };
	OK(futimens(fd, now));
	OK(fstat(fd, &st));
	CHECK(st.st_atim.tv_sec > 1000000000 && st.st_mtim.tv_sec == 3000);
	OK(utimensat(AT_FDCWD, "/tmp/f", NULL, 0));
	OK(stat("/tmp/f", &st));
	CHECK(st.st_mtim.tv_sec > 1000000000);
	struct timespec bad[2] = { { 0, 1000000000 }, { 0, 0 } };
	FAILS(utimensat(AT_FDCWD, "/tmp/f", bad, 0), EINVAL);
	OK(symlink("f", "/tmp/l"));
	OK(utimensat(AT_FDCWD, "/tmp/l", ts, AT_SYMLINK_NOFOLLOW));
	OK(lstat("/tmp/l", &st));
	CHECK(st.st_mtim.tv_sec == 2000);
	OK(stat("/tmp/l", &st));
	CHECK(st.st_mtim.tv_sec != 2000);

	/* fallocate. */
	OK(ftruncate(fd, 0));
	OK(fallocate(fd, 0, 0, 100));
	OK(fstat(fd, &st));
	CHECK(st.st_size == 100);
	OK(fallocate(fd, FALLOC_FL_KEEP_SIZE, 0, 200));
	OK(fstat(fd, &st));
	CHECK(st.st_size == 100);
	CHECK(pwrite(fd, "xxxxxxxxxx", 10, 0) == 10);
	OK(fallocate(fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, 2, 4));
	CHECK(pread(fd, buf, 10, 0) == 10);
	CHECK(!memcmp(buf, "xx\0\0\0\0xxxx", 10));
	FAILS(fallocate(fd, FALLOC_FL_PUNCH_HOLE, 0, 1), EOPNOTSUPP);
	FAILS(fallocate(fd, 0, 0, 0), EINVAL);
	FAILS(fallocate(fd, 0, -1, 1), EINVAL);
	CHECK(posix_fallocate(fd, 0, 300) == 0);
	OK(fstat(fd, &st));
	CHECK(st.st_size == 300);
	int ro = open("/tmp/f", O_RDONLY);
	FAILS(fallocate(ro, 0, 0, 1), EBADF);
	close(ro);

	OK(fsync(fd));
	OK(fdatasync(fd));
	sync();

	/* statfs. */
	struct statfs sf;
	OK(statfs("/tmp", &sf));
	CHECK(sf.f_bsize > 0 && sf.f_namelen == 255);
	struct statfs sf2;
	OK(fstatfs(fd, &sf2));
	CHECK(sf2.f_type == sf.f_type);
	struct statvfs sv;
	OK(statvfs("/", &sv));
	CHECK(sv.f_namemax == 255 && !(sv.f_flag & ST_RDONLY));
	FAILS(statfs("/tmp/none", &sf), ENOENT);

	close(fd);
	if (fails)
		return 1;
	puts("PASS");
	return 0;
}
