// lseek: SEEK_SET/CUR/END, invalid offsets and whence, seeking past the end, and
// SEEK_DATA/SEEK_HOLE on a sparse file.
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
	char buf[16];
	int fd = open("/tmp/f", O_CREAT | O_RDWR, 0644);
	CHECK(fd >= 0);
	CHECK(write(fd, "0123456789", 10) == 10);

	CHECK(lseek(fd, 2, SEEK_SET) == 2);
	CHECK(lseek(fd, 3, SEEK_CUR) == 5);
	CHECK(lseek(fd, -1, SEEK_END) == 9);
	CHECK(read(fd, buf, 4) == 1 && buf[0] == '9');
	FAILS(lseek(fd, -1, SEEK_SET), EINVAL);
	FAILS(lseek(fd, -20, SEEK_END), EINVAL);
	FAILS(lseek(fd, 0, 42), EINVAL);
	CHECK(lseek(fd, 0, SEEK_CUR) == 10);

	/* Past the end is allowed; a write there leaves a hole. */
	off_t far = 1 << 20;
	CHECK(lseek(fd, far, SEEK_SET) == far);
	CHECK(read(fd, buf, 4) == 0);
	CHECK(write(fd, "x", 1) == 1);
	struct stat st;
	OK(fstat(fd, &st));
	CHECK(st.st_size == far + 1);

	/* SEEK_DATA / SEEK_HOLE: data at 0, a hole somewhere before the second write, data at
	 * the end, and the implicit hole at EOF. */
	CHECK(lseek(fd, 0, SEEK_DATA) == 0);
	off_t hole = lseek(fd, 0, SEEK_HOLE);
	CHECK(hole >= 10 && hole <= far);
	off_t data = lseek(fd, hole, SEEK_DATA);
	CHECK(data > hole && data <= far);
	CHECK(lseek(fd, far, SEEK_HOLE) == far + 1);
	CHECK(lseek(fd, far + 1, SEEK_HOLE) == -1 && errno == ENXIO);
	FAILS(lseek(fd, far + 1, SEEK_DATA), ENXIO);
	FAILS(lseek(fd, -1, SEEK_DATA), ENXIO);
	CHECK(lseek(fd, 0, SEEK_CUR) == far + 1);
	close(fd);

	/* Directories can seek back to the start. */
	fd = open("/tmp", O_RDONLY | O_DIRECTORY);
	CHECK(lseek(fd, 0, SEEK_SET) == 0);
	close(fd);

	FAILS(lseek(999, 0, SEEK_SET), EBADF);

	if (fails)
		return 1;
	puts("PASS");
	return 0;
}
