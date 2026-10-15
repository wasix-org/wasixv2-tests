// read/write, pread/pwrite, readv/writev and preadv/pwritev: offsets, holes, EOF and the
// errors for bad offsets and unseekable files.
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: CHECK(%s) failed (errno %s)\n", __LINE__, #c, strerror(errno)); fails++; } } while (0)
#define OK(call) do { errno = 0; if ((long)(call) < 0) { fprintf(stderr, "line %d: %s failed: %s\n", __LINE__, #call, strerror(errno)); fails++; } } while (0)
#define FAILS(call, e) do { errno = 0; long r_ = (long)(call); if (r_ != -1 || errno != (e)) { fprintf(stderr, "line %d: %s = %ld (%s), want -1 %s\n", __LINE__, #call, r_, strerror(errno), #e); fails++; } } while (0)

int main(void)
{
	char buf[256];
	int fd = open("/tmp/f", O_CREAT | O_RDWR, 0644);
	CHECK(fd >= 0);

	CHECK(write(fd, "0123456789", 10) == 10);
	CHECK(lseek(fd, 0, SEEK_CUR) == 10);
	CHECK(read(fd, buf, sizeof buf) == 0);

	/* pread/pwrite leave the offset alone. */
	CHECK(pread(fd, buf, 4, 3) == 4);
	CHECK(memcmp(buf, "3456", 4) == 0);
	CHECK(pwrite(fd, "AB", 2, 1) == 2);
	CHECK(lseek(fd, 0, SEEK_CUR) == 10);
	CHECK(pread(fd, buf, 100, 0) == 10);
	CHECK(memcmp(buf, "0AB3456789", 10) == 0);
	CHECK(pread(fd, buf, 10, 100) == 0);
	FAILS(pread(fd, buf, 1, -1), EINVAL);
	FAILS(pwrite(fd, buf, 1, -1), EINVAL);

	/* Writing past the end leaves a hole of zeros. */
	CHECK(pwrite(fd, "Z", 1, 20) == 1);
	CHECK(pread(fd, buf, 100, 0) == 21);
	for (int i = 10; i < 20; i++)
		CHECK(buf[i] == 0);
	CHECK(buf[20] == 'Z');

	/* Vectored I/O. */
	struct iovec iov[3] = {
		{ "ab", 2 },
		{ NULL, 0 },
		{ "cde", 3 },
	};
	CHECK(lseek(fd, 0, SEEK_SET) == 0);
	CHECK(writev(fd, iov, 3) == 5);
	CHECK(lseek(fd, 0, SEEK_CUR) == 5);
	char a[2], b[4];
	struct iovec riov[2] = { { a, 2 }, { b, 4 } };
	CHECK(lseek(fd, 0, SEEK_SET) == 0);
	CHECK(readv(fd, riov, 2) == 6);
	CHECK(memcmp(a, "ab", 2) == 0 && memcmp(b, "cde5", 4) == 0);
	CHECK(lseek(fd, 0, SEEK_CUR) == 6);
	CHECK(pwritev(fd, iov, 3, 30) == 5);
	CHECK(preadv(fd, riov, 2, 29) == 6);
	CHECK(a[0] == 0 && a[1] == 'a' && memcmp(b, "bcde", 4) == 0);
	CHECK(lseek(fd, 0, SEEK_CUR) == 6);
	FAILS(readv(fd, riov, -1), EINVAL);

	/* Zero-length transfers. */
	CHECK(write(fd, buf, 0) == 0);
	CHECK(read(fd, buf, 0) == 0);

	/* Bad descriptors. */
	FAILS(read(999, buf, 1), EBADF);
	FAILS(write(-1, buf, 1), EBADF);
	FAILS(pread(999, buf, 1, 0), EBADF);
	close(fd);

	/* write with O_APPEND appends whatever the offset. */
	fd = open("/tmp/f", O_RDWR | O_APPEND);
	struct stat st;
	OK(fstat(fd, &st));
	CHECK(lseek(fd, 0, SEEK_SET) == 0);
	CHECK(write(fd, "X", 1) == 1);
	CHECK(pread(fd, buf, 1, st.st_size) == 1 && buf[0] == 'X');
	close(fd);

	/* Pipes cannot seek. */
	int p[2];
	OK(pipe(p));
	FAILS(lseek(p[0], 0, SEEK_CUR), ESPIPE);
	FAILS(pread(p[0], buf, 1, 0), ESPIPE);
	FAILS(pwrite(p[1], buf, 1, 0), ESPIPE);

	if (fails)
		return 1;
	puts("PASS");
	return 0;
}
