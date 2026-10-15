// access/faccessat/faccessat2 for an unprivileged user: mode bits, search permission on
// directories, AT_EACCESS, AT_SYMLINK_NOFOLLOW and the argument errors.
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
	if (getuid() == 0) {
		puts("PASS");
		return 0;
	}
	int fd = open("/tmp/f", O_CREAT | O_WRONLY, 0644);
	close(fd);

	OK(access("/tmp/f", F_OK));
	OK(access("/tmp/f", R_OK | W_OK));
	FAILS(access("/tmp/f", X_OK), EACCES);
	FAILS(access("/tmp/missing", F_OK), ENOENT);
	FAILS(access("/tmp/f", 0x10), EINVAL);

	OK(chmod("/tmp/f", 0));
	FAILS(access("/tmp/f", R_OK), EACCES);
	FAILS(access("/tmp/f", W_OK), EACCES);
	OK(access("/tmp/f", F_OK));
	FAILS(open("/tmp/f", O_RDONLY), EACCES);

	OK(chmod("/tmp/f", 0500));
	OK(access("/tmp/f", R_OK | X_OK));
	FAILS(access("/tmp/f", W_OK), EACCES);

	/* Search permission on the directory. */
	OK(mkdir("/tmp/d", 0755));
	fd = open("/tmp/d/g", O_CREAT | O_WRONLY, 0644);
	close(fd);
	OK(chmod("/tmp/d", 0644));
	FAILS(access("/tmp/d/g", F_OK), EACCES);
	struct stat st;
	FAILS(stat("/tmp/d/g", &st), EACCES);
	OK(chmod("/tmp/d", 0311));
	OK(access("/tmp/d/g", R_OK));
	OK(chmod("/tmp/d", 0755));

	/* faccessat variants. */
	int dfd = open("/tmp", O_RDONLY | O_DIRECTORY);
	OK(faccessat(dfd, "f", R_OK, 0));
	OK(faccessat(dfd, "f", R_OK, AT_EACCESS));
	FAILS(faccessat(dfd, "f", R_OK, 0x1000000), EINVAL);
	OK(symlink("missing", "/tmp/dangling"));
	FAILS(faccessat(dfd, "dangling", F_OK, 0), ENOENT);
	OK(faccessat(dfd, "dangling", F_OK, AT_SYMLINK_NOFOLLOW));
	close(dfd);

	if (fails)
		return 1;
	puts("PASS");
	return 0;
}
