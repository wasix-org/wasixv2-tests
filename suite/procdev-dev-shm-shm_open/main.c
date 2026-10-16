// POSIX shared memory objects live in /dev/shm, a world-writable sticky tmpfs: shm_open
// creates one there, O_EXCL refuses a second, the object keeps its data across opens, and
// shm_unlink removes it.
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <unistd.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: CHECK(%s) failed (errno %s)\n", __LINE__, #c, strerror(errno)); fails++; } } while (0)
#define FAILS(call, e) do { errno = 0; long r_ = (long)(call); if (r_ != -1 || errno != (e)) { fprintf(stderr, "line %d: %s = %ld (%s), want -1 %s\n", __LINE__, #call, r_, strerror(errno), #e); fails++; } } while (0)

#define TMPFS_MAGIC 0x01021994

int main(void)
{
	struct stat st;
	struct statfs fs;
	CHECK(stat("/dev/shm", &st) == 0);
	CHECK(S_ISDIR(st.st_mode) && (st.st_mode & 07777) == 01777);
	CHECK(statfs("/dev/shm", &fs) == 0 && fs.f_type == TMPFS_MAGIC);

	int fd = shm_open("/procdev-test", O_CREAT | O_EXCL | O_RDWR, 0600);
	CHECK(fd >= 0);
	CHECK(ftruncate(fd, 4096) == 0);
	CHECK(write(fd, "shared", 6) == 6);
	CHECK(stat("/dev/shm/procdev-test", &st) == 0);
	CHECK(S_ISREG(st.st_mode) && st.st_size == 4096 && (st.st_mode & 0777) == 0600);
	FAILS(shm_open("/procdev-test", O_CREAT | O_EXCL | O_RDWR, 0600), EEXIST);
	close(fd);

	char buf[8] = {0};
	fd = shm_open("/procdev-test", O_RDONLY, 0);
	CHECK(fd >= 0);
	CHECK(read(fd, buf, 6) == 6 && !memcmp(buf, "shared", 6));
	close(fd);

	CHECK(shm_unlink("/procdev-test") == 0);
	FAILS(stat("/dev/shm/procdev-test", &st), ENOENT);
	FAILS(shm_open("/procdev-test", O_RDONLY, 0), ENOENT);
	FAILS(shm_unlink("/procdev-test"), ENOENT);

	if (fails)
		return 1;
	puts("PASS");
	return 0;
}
