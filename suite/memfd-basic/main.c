// memfd_create: an unnamed read-write file with a /proc link name, its flags and name checks,
// and file seals (F_ADD_SEALS, F_GET_SEALS) on writes, size changes, mode changes and mmap.
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef F_SEAL_EXEC
#define F_SEAL_EXEC 0x0020
#endif
#ifndef MFD_NOEXEC_SEAL
#define MFD_NOEXEC_SEAL 0x0008U
#endif
#ifndef MFD_EXEC
#define MFD_EXEC 0x0010U
#endif

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: CHECK(%s) failed (errno %s)\n", __LINE__, #c, strerror(errno)); fails++; } } while (0)
#define OK(call) do { errno = 0; if ((long)(call) < 0) { fprintf(stderr, "line %d: %s failed: %s\n", __LINE__, #call, strerror(errno)); fails++; } } while (0)
#define FAILS(call, e) do { errno = 0; long r_ = (long)(call); if (r_ != -1 || errno != (e)) { fprintf(stderr, "line %d: %s = %ld (%s), want -1 %s\n", __LINE__, #call, r_, strerror(errno), #e); fails++; } } while (0)

int main(void)
{
	struct stat st;
	char buf[64];

	/* An unnamed, empty, read-write file. */
	int fd = memfd_create("test", MFD_CLOEXEC | MFD_ALLOW_SEALING);
	CHECK(fd >= 0);
	CHECK(fcntl(fd, F_GETFD) == FD_CLOEXEC);
	CHECK((fcntl(fd, F_GETFL) & O_ACCMODE) == O_RDWR);
	OK(fstat(fd, &st));
	CHECK(S_ISREG(st.st_mode) && (st.st_mode & 07777) == 0777);
	CHECK(st.st_nlink == 0 && st.st_size == 0);
	CHECK(write(fd, "hello world", 11) == 11);
	CHECK(lseek(fd, 0, SEEK_SET) == 0);
	CHECK(read(fd, buf, sizeof buf) == 11 && memcmp(buf, "hello world", 11) == 0);
	OK(ftruncate(fd, 4096));
	OK(fstat(fd, &st));
	CHECK(st.st_size == 4096);
	char link[64];
	snprintf(buf, sizeof buf, "/proc/self/fd/%d", fd);
	ssize_t n = readlink(buf, link, sizeof link);
	CHECK(n == 21 && memcmp(link, "/memfd:test (deleted)", 21) == 0);

	/* Flags and the name. */
	FAILS(memfd_create("x", 0x40), EINVAL);
	FAILS(memfd_create("x", MFD_EXEC | MFD_NOEXEC_SEAL), EINVAL);
	char name[251];
	memset(name, 'n', 250);
	name[250] = 0;
	FAILS(memfd_create(name, 0), EINVAL);
	name[249] = 0;
	int named = memfd_create(name, 0);
	CHECK(named >= 0);
	close(named);

	/* Seals, added one by one. */
	CHECK(fcntl(fd, F_GET_SEALS) == 0);
	FAILS(fcntl(fd, F_ADD_SEALS, 0x40), EINVAL);
	OK(fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK));
	FAILS(ftruncate(fd, 100), EPERM);
	OK(ftruncate(fd, 8192));
	OK(fcntl(fd, F_ADD_SEALS, F_SEAL_GROW));
	FAILS(ftruncate(fd, 8193), EPERM);
	FAILS(pwrite(fd, "x", 1, 8192), EPERM);
	FAILS(fallocate(fd, 0, 8000, 1000), EPERM);
	CHECK(pwrite(fd, "abc", 3, 100) == 3);
	OK(fcntl(fd, F_ADD_SEALS, F_SEAL_WRITE));
	FAILS(pwrite(fd, "abc", 3, 100), EPERM);
	FAILS(write(fd, "abc", 3), EPERM);
	FAILS(fallocate(fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, 0, 100), EPERM);
	FAILS((long)mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0), EPERM);
	CHECK(pread(fd, buf, 3, 100) == 3 && memcmp(buf, "abc", 3) == 0);
	OK(fcntl(fd, F_ADD_SEALS, F_SEAL_SEAL));
	FAILS(fcntl(fd, F_ADD_SEALS, F_SEAL_FUTURE_WRITE), EPERM);
	CHECK(fcntl(fd, F_GET_SEALS) == (F_SEAL_SEAL | F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE));
	close(fd);

	/* Without MFD_ALLOW_SEALING, the file is sealed against seals. */
	fd = memfd_create("plain", 0);
	CHECK(fd >= 0);
	CHECK(fcntl(fd, F_GET_SEALS) == F_SEAL_SEAL);
	FAILS(fcntl(fd, F_ADD_SEALS, F_SEAL_WRITE), EPERM);
	close(fd);

	/* MFD_NOEXEC_SEAL: not executable, and never made so. */
	fd = memfd_create("noexec", MFD_NOEXEC_SEAL);
	CHECK(fd >= 0);
	OK(fstat(fd, &st));
	CHECK((st.st_mode & 07777) == 0666);
	CHECK(fcntl(fd, F_GET_SEALS) == F_SEAL_EXEC);
	FAILS(fchmod(fd, 0755), EPERM);
	OK(fchmod(fd, 0600));
	close(fd);

	/* Seals belong to tmpfs files; a pipe has none. */
	int p[2];
	OK(pipe(p));
	FAILS(fcntl(p[0], F_GET_SEALS), EINVAL);

	if (fails)
		return 1;
	puts("PASS");
	return 0;
}
