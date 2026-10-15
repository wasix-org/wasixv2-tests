// rename/renameat2: replacing files and directories, the type and emptiness errors, moving a
// directory into itself, RENAME_NOREPLACE and RENAME_EXCHANGE.
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

static void put(const char *path, const char *text)
{
	int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
	write(fd, text, strlen(text));
	close(fd);
}

static int is(const char *path, const char *text)
{
	char buf[64] = { 0 };
	int fd = open(path, O_RDONLY);
	if (fd < 0)
		return 0;
	read(fd, buf, sizeof buf - 1);
	close(fd);
	return strcmp(buf, text) == 0;
}

int main(void)
{
	struct stat st;
	put("/tmp/a", "a");
	put("/tmp/b", "b");
	OK(rename("/tmp/a", "/tmp/b"));
	CHECK(is("/tmp/b", "b") == 0 && is("/tmp/b", "a"));
	FAILS(stat("/tmp/a", &st), ENOENT);
	FAILS(rename("/tmp/a", "/tmp/c"), ENOENT);

	OK(mkdir("/tmp/d", 0755));
	OK(mkdir("/tmp/e", 0755));
	FAILS(rename("/tmp/b", "/tmp/d"), EISDIR);
	FAILS(rename("/tmp/d", "/tmp/b"), ENOTDIR);
	put("/tmp/e/x", "x");
	FAILS(rename("/tmp/d", "/tmp/e"), ENOTEMPTY);
	OK(unlink("/tmp/e/x"));
	OK(rename("/tmp/d", "/tmp/e"));
	FAILS(stat("/tmp/d", &st), ENOENT);
	OK(mkdir("/tmp/e/sub", 0755));
	FAILS(rename("/tmp/e", "/tmp/e/sub/x"), EINVAL);
	FAILS(rename("/tmp/e/.", "/tmp/z"), EBUSY);

	/* Same object: a no-op that leaves both names. */
	OK(rename("/tmp/b", "/tmp/b"));
	OK(link("/tmp/b", "/tmp/b2"));
	OK(rename("/tmp/b", "/tmp/b2"));
	OK(stat("/tmp/b", &st));
	CHECK(st.st_nlink == 2);

	/* Across directories, and the parent's link count follows a moved directory. */
	OK(rename("/tmp/e/sub", "/tmp/sub"));
	OK(stat("/tmp/e", &st));
	CHECK(st.st_nlink == 2);

	/* renameat2. */
	put("/tmp/p", "p");
	put("/tmp/q", "q");
	FAILS(renameat2(AT_FDCWD, "/tmp/p", AT_FDCWD, "/tmp/q", RENAME_NOREPLACE), EEXIST);
	OK(renameat2(AT_FDCWD, "/tmp/p", AT_FDCWD, "/tmp/r", RENAME_NOREPLACE));
	OK(renameat2(AT_FDCWD, "/tmp/r", AT_FDCWD, "/tmp/q", RENAME_EXCHANGE));
	CHECK(is("/tmp/r", "q") && is("/tmp/q", "p"));
	OK(renameat2(AT_FDCWD, "/tmp/r", AT_FDCWD, "/tmp/e", RENAME_EXCHANGE));
	OK(stat("/tmp/r", &st));
	CHECK(S_ISDIR(st.st_mode));
	CHECK(is("/tmp/e", "q"));
	FAILS(renameat2(AT_FDCWD, "/tmp/q", AT_FDCWD, "/tmp/none", RENAME_EXCHANGE), ENOENT);
	FAILS(renameat2(AT_FDCWD, "/tmp/q", AT_FDCWD, "/tmp/e", RENAME_EXCHANGE | RENAME_NOREPLACE),
	      EINVAL);
	FAILS(renameat2(AT_FDCWD, "/tmp/q", AT_FDCWD, "/tmp/e", 0x100), EINVAL);
	if (getuid() != 0)
		FAILS(renameat2(AT_FDCWD, "/tmp/q", AT_FDCWD, "/tmp/w", RENAME_WHITEOUT), EPERM);

	if (fails)
		return 1;
	puts("PASS");
	return 0;
}
