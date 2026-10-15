// Extended attributes in the user namespace: set/get/list/remove, size queries, ERANGE,
// XATTR_CREATE/XATTR_REPLACE, symlinks and unknown namespaces.
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: CHECK(%s) failed (errno %s)\n", __LINE__, #c, strerror(errno)); fails++; } } while (0)
#define OK(call) do { errno = 0; if ((long)(call) < 0) { fprintf(stderr, "line %d: %s failed: %s\n", __LINE__, #call, strerror(errno)); fails++; } } while (0)
#define FAILS(call, e) do { errno = 0; long r_ = (long)(call); if (r_ != -1 || errno != (e)) { fprintf(stderr, "line %d: %s = %ld (%s), want -1 %s\n", __LINE__, #call, r_, strerror(errno), #e); fails++; } } while (0)

int main(void)
{
	char buf[128];
	int fd = open("/tmp/f", O_CREAT | O_RDWR, 0644);

	OK(setxattr("/tmp/f", "user.a", "hello", 5, 0));
	CHECK(getxattr("/tmp/f", "user.a", NULL, 0) == 5);
	CHECK(getxattr("/tmp/f", "user.a", buf, sizeof buf) == 5 && !memcmp(buf, "hello", 5));
	FAILS(getxattr("/tmp/f", "user.a", buf, 2), ERANGE);
	FAILS(getxattr("/tmp/f", "user.none", buf, sizeof buf), ENODATA);
	FAILS(setxattr("/tmp/f", "user.a", "x", 1, XATTR_CREATE), EEXIST);
	FAILS(setxattr("/tmp/f", "user.b", "x", 1, XATTR_REPLACE), ENODATA);
	OK(setxattr("/tmp/f", "user.a", "bye", 3, XATTR_REPLACE));
	OK(fsetxattr(fd, "user.b", "", 0, XATTR_CREATE));
	CHECK(fgetxattr(fd, "user.a", buf, sizeof buf) == 3 && !memcmp(buf, "bye", 3));
	CHECK(fgetxattr(fd, "user.b", buf, sizeof buf) == 0);

	ssize_t n = listxattr("/tmp/f", buf, sizeof buf);
	CHECK(n == 14);
	CHECK(flistxattr(fd, NULL, 0) == 14);
	FAILS(listxattr("/tmp/f", buf, 3), ERANGE);
	int a = 0, b = 0;
	for (char *p = buf; p < buf + n; p += strlen(p) + 1) {
		a += !strcmp(p, "user.a");
		b += !strcmp(p, "user.b");
	}
	CHECK(a == 1 && b == 1);

	OK(removexattr("/tmp/f", "user.a"));
	FAILS(removexattr("/tmp/f", "user.a"), ENODATA);
	OK(fremovexattr(fd, "user.b"));
	CHECK(listxattr("/tmp/f", buf, sizeof buf) == 0);

	FAILS(setxattr("/tmp/f", "bogus.a", "x", 1, 0), EOPNOTSUPP);
	FAILS(setxattr("/tmp/f", "user.a", "x", 1, 0x10), EINVAL);
	FAILS(getxattr("/tmp/missing", "user.a", buf, sizeof buf), ENOENT);

	/* user.* is not allowed on symlinks; getxattr follows them. */
	OK(symlink("f", "/tmp/l"));
	FAILS(lsetxattr("/tmp/l", "user.a", "x", 1, 0), EPERM);
	OK(setxattr("/tmp/l", "user.c", "via", 3, 0));
	CHECK(getxattr("/tmp/f", "user.c", buf, sizeof buf) == 3);
	FAILS(lgetxattr("/tmp/l", "user.c", buf, sizeof buf), ENODATA);

	if (fails)
		return 1;
	puts("PASS");
	return 0;
}
