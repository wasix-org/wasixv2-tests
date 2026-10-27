// openat2 with RESOLVE_IN_ROOT: absolute paths, absolute symlinks and ".." resolve inside dfd,
// not inside the working directory, and dfd is checked even for an absolute path.
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
#define RESOLVE_IN_ROOT 0x10

static long openat2(int dfd, const char *path, uint64_t resolve)
{
	struct open_how how = { O_RDONLY, 0, resolve };
	return syscall(SYS_openat2, dfd, path, &how, sizeof how);
}

static void put(const char *path, const char *data)
{
	int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
	CHECK(fd >= 0);
	CHECK(write(fd, data, strlen(data)) == (ssize_t)strlen(data));
	close(fd);
}

/* What `path` names under `dfd` with RESOLVE_IN_ROOT, as its contents. */
static const char *in_root(int dfd, const char *path)
{
	static char buf[16];
	int fd = openat2(dfd, path, RESOLVE_IN_ROOT);
	if (fd < 0)
		return strerror(errno);
	ssize_t n = read(fd, buf, sizeof buf - 1);
	close(fd);
	buf[n < 0 ? 0 : n] = 0;
	return buf;
}

int main(void)
{
	const char *dirs[] = { "/tmp/cwd", "/tmp/cwd/etc", "/tmp/jail", "/tmp/jail/etc", "/tmp/jail/sub" };
	for (unsigned i = 0; i < sizeof dirs / sizeof *dirs; i++)
		OK(mkdir(dirs[i], 0755));
	put("/tmp/cwd/etc/passwd", "cwd");
	put("/tmp/jail/etc/passwd", "jail");
	put("/tmp/file", "");
	OK(symlink("/etc/passwd", "/tmp/jail/abs"));
	OK(symlink("../../../etc/passwd", "/tmp/jail/sub/up"));
	OK(chdir("/tmp/cwd"));
	int jail = open("/tmp/jail", O_RDONLY | O_DIRECTORY);
	CHECK(jail >= 0);

	CHECK(strcmp(in_root(jail, "/etc/passwd"), "jail") == 0);
	CHECK(strcmp(in_root(jail, "//etc/../../etc/passwd"), "jail") == 0);
	CHECK(strcmp(in_root(jail, "abs"), "jail") == 0);
	CHECK(strcmp(in_root(jail, "sub/up"), "jail") == 0);
	/* Without a descriptor, the root is the working directory. */
	CHECK(strcmp(in_root(AT_FDCWD, "/etc/passwd"), "cwd") == 0);
	FAILS(openat2(1000, "/etc/passwd", RESOLVE_IN_ROOT), EBADF);
	int file = open("/tmp/file", O_RDONLY);
	FAILS(openat2(file, "/", RESOLVE_IN_ROOT), ENOTDIR);

	if (fails)
		return 1;
	puts("PASS");
	return 0;
}
