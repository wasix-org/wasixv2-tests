// /tmp has a size limit like tmpfs: an fallocate past it fails at once with ENOSPC instead of
// exhausting the host, and huge allocations, truncations and hole punches cost nothing.
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: CHECK(%s) failed (errno %s)\n", __LINE__, #c, strerror(errno)); fails++; } } while (0)
#define OK(call) do { errno = 0; if ((long)(call) < 0) { fprintf(stderr, "line %d: %s failed: %s\n", __LINE__, #call, strerror(errno)); fails++; } } while (0)
#define FAILS(call, e) do { errno = 0; long r_ = (long)(call); if (r_ != -1 || errno != (e)) { fprintf(stderr, "line %d: %s = %ld (%s), want -1 %s\n", __LINE__, #call, r_, strerror(errno), #e); fails++; } } while (0)

int main(void)
{
	const off_t tera = (off_t)1 << 40;
	struct stat st;
	struct statvfs sv;

	OK(statvfs("/tmp", &sv));
	CHECK(sv.f_blocks > 0 && sv.f_bfree <= sv.f_blocks && sv.f_files > 0);
	off_t size = (off_t)sv.f_blocks * sv.f_frsize;
	CHECK(size < tera);

	int fd = open("/tmp/big", O_CREAT | O_RDWR, 0644);
	CHECK(fd >= 0);
	/* More than the filesystem holds: refused up front, nothing allocated. */
	FAILS(fallocate(fd, 0, 0, tera), ENOSPC);
	FAILS(fallocate(fd, FALLOC_FL_KEEP_SIZE, 0, size + 4096), ENOSPC);
	OK(fstat(fd, &st));
	CHECK(st.st_size == 0 && st.st_blocks == 0);
	struct statvfs after;
	OK(statvfs("/tmp", &after));
	CHECK(after.f_bfree == sv.f_bfree);

	/* A sparse terabyte, punched across its whole length. */
	OK(ftruncate(fd, tera));
	OK(fallocate(fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, 0, tera));
	OK(fstat(fd, &st));
	CHECK(st.st_size == tera && st.st_blocks == 0);

	/* Allocated blocks count as used and read as zeroes; they are holes to SEEK_DATA. */
	OK(fallocate(fd, FALLOC_FL_KEEP_SIZE, 0, 1 << 20));
	OK(fstat(fd, &st));
	CHECK(st.st_blocks == (1 << 20) / 512);
	OK(statvfs("/tmp", &after));
	CHECK(after.f_bfree == sv.f_bfree - (1 << 20) / sv.f_frsize);
	char buf[4] = { 1, 1, 1, 1 };
	CHECK(pread(fd, buf, 4, 4096) == 4 && !memcmp(buf, "\0\0\0\0", 4));
	FAILS(lseek(fd, 0, SEEK_DATA), ENXIO);
	/* Shrinking frees them again. */
	OK(ftruncate(fd, 0));
	OK(fstat(fd, &st));
	CHECK(st.st_blocks == 0);

	/* A zero-length write past the end does not extend the file. */
	CHECK(pwrite(fd, "", 0, 1 << 20) == 0);
	OK(fstat(fd, &st));
	CHECK(st.st_size == 0);

	close(fd);
	OK(unlink("/tmp/big"));
	if (fails)
		return 1;
	puts("PASS");
	return 0;
}
