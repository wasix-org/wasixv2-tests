// O_TMPFILE (unnamed files, linking them in, O_EXCL) and openat2's RESOLVE_* scopes and
// argument checks.
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: CHECK(%s) failed (errno %s)\n", __LINE__, #c, strerror(errno)); fails++; } } while (0)
#define OK(call) do { errno = 0; if ((long)(call) < 0) { fprintf(stderr, "line %d: %s failed: %s\n", __LINE__, #call, strerror(errno)); fails++; } } while (0)
#define FAILS(call, e) do { errno = 0; long r_ = (long)(call); if (r_ != -1 || errno != (e)) { fprintf(stderr, "line %d: %s = %ld (%s), want -1 %s\n", __LINE__, #call, r_, strerror(errno), #e); fails++; } } while (0)

struct open_how {
	uint64_t flags, mode, resolve;
};
#define RESOLVE_NO_XDEV 0x01
#define RESOLVE_NO_MAGICLINKS 0x02
#define RESOLVE_NO_SYMLINKS 0x04
#define RESOLVE_BENEATH 0x08
#define RESOLVE_IN_ROOT 0x10

static long openat2(int dfd, const char *path, uint64_t flags, uint64_t resolve)
{
	struct open_how how = { flags, 0, resolve };
	return syscall(SYS_openat2, dfd, path, &how, sizeof how);
}

int main(void)
{
	struct stat st;
	char buf[16];

	/* O_TMPFILE. */
	int fd = open("/tmp", O_TMPFILE | O_RDWR, 0600);
	CHECK(fd >= 0);
	OK(fstat(fd, &st));
	CHECK(S_ISREG(st.st_mode) && st.st_nlink == 0);
	CHECK(write(fd, "data", 4) == 4);
	CHECK(pread(fd, buf, sizeof buf, 0) == 4);
	FAILS(open("/tmp", O_TMPFILE | O_RDONLY, 0600), EINVAL);
	FAILS(open("/tmp/none", O_TMPFILE | O_RDWR, 0600), ENOENT);
	OK(linkat(fd, "", AT_FDCWD, "/tmp/named", AT_EMPTY_PATH));
	OK(stat("/tmp/named", &st));
	CHECK(st.st_nlink == 1 && st.st_size == 4);
	close(fd);
	fd = open("/tmp", O_TMPFILE | O_RDWR | O_EXCL, 0600);
	CHECK(fd >= 0);
	FAILS(linkat(fd, "", AT_FDCWD, "/tmp/named2", AT_EMPTY_PATH), ENOENT);
	close(fd);

	/* openat2. */
	OK(mkdir("/tmp/root", 0755));
	OK(mkdir("/tmp/root/sub", 0755));
	close(open("/tmp/root/file", O_CREAT | O_WRONLY, 0644));
	close(open("/tmp/outside", O_CREAT | O_WRONLY, 0644));
	OK(symlink("/file", "/tmp/root/abs"));
	OK(symlink("../outside", "/tmp/root/up"));
	int dfd = open("/tmp/root", O_RDONLY | O_DIRECTORY);

	fd = openat2(dfd, "file", O_RDONLY, 0);
	CHECK(fd >= 0);
	close(fd);
	fd = openat2(dfd, "sub/../file", O_RDONLY, RESOLVE_BENEATH);
	CHECK(fd >= 0);
	close(fd);
	FAILS(openat2(dfd, "../outside", O_RDONLY, RESOLVE_BENEATH), EXDEV);
	FAILS(openat2(dfd, "/tmp/outside", O_RDONLY, RESOLVE_BENEATH), EXDEV);
	FAILS(openat2(dfd, "up", O_RDONLY, RESOLVE_BENEATH), EXDEV);
	FAILS(openat2(dfd, "abs", O_RDONLY, RESOLVE_BENEATH), EXDEV);
	/* RESOLVE_IN_ROOT: "/" and ".." stay at dfd. */
	fd = openat2(dfd, "abs", O_RDONLY, RESOLVE_IN_ROOT);
	CHECK(fd >= 0);
	close(fd);
	fd = openat2(dfd, "../../file", O_RDONLY, RESOLVE_IN_ROOT);
	CHECK(fd >= 0);
	close(fd);
	FAILS(openat2(dfd, "abs", O_RDONLY, RESOLVE_NO_SYMLINKS), ELOOP);
	FAILS(openat2(dfd, "file", O_RDONLY, RESOLVE_BENEATH | RESOLVE_IN_ROOT), EINVAL);
	FAILS(openat2(dfd, "file", O_RDONLY, 0x1000), EINVAL);
	FAILS(openat2(dfd, "file", O_RDONLY | 0x40000000, 0), EINVAL);
	struct open_how how = { O_RDONLY, 0644, 0 };
	FAILS(syscall(SYS_openat2, dfd, "file", &how, sizeof how), EINVAL);
	how.mode = 0;
	FAILS(syscall(SYS_openat2, dfd, "file", &how, 8), EINVAL);
	struct {
		struct open_how how;
		uint64_t extra;
	} big = { { O_RDONLY, 0, 0 }, 1 };
	FAILS(syscall(SYS_openat2, dfd, "file", &big, sizeof big), E2BIG);
	big.extra = 0;
	fd = syscall(SYS_openat2, dfd, "file", &big, sizeof big);
	CHECK(fd >= 0);
	close(fd);

	if (fails)
		return 1;
	puts("PASS");
	return 0;
}
