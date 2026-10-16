// realpath resolves symlinks, `.`, `..` and the magic links of /proc: through
// /proc/self/cwd and /proc/self/fd/N into the directories they name.
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: CHECK(%s) failed (errno %s)\n", __LINE__, #c, strerror(errno)); fails++; } } while (0)

static void expect(const char *path, const char *want)
{
	char out[PATH_MAX];
	errno = 0;
	if (!realpath(path, out)) {
		fprintf(stderr, "realpath(%s): %s\n", path, strerror(errno));
		fails++;
	} else if (strcmp(out, want)) {
		fprintf(stderr, "realpath(%s) = %s, want %s\n", path, out, want);
		fails++;
	}
}

int main(void)
{
	CHECK(mkdir("/tmp/a", 0755) == 0);
	CHECK(mkdir("/tmp/a/b", 0755) == 0);
	CHECK(close(open("/tmp/a/b/f", O_CREAT | O_WRONLY, 0644)) == 0);
	CHECK(symlink("a/b", "/tmp/link") == 0);
	CHECK(symlink("/tmp/a/b/f", "/tmp/a/abs") == 0);
	CHECK(chdir("/tmp/a") == 0);

	expect("/tmp/link/f", "/tmp/a/b/f");
	expect("/tmp/a/./b/../b/f", "/tmp/a/b/f");
	expect("abs", "/tmp/a/b/f");
	expect("b", "/tmp/a/b");
	expect("/proc/self/cwd", "/tmp/a");
	expect("/proc/self/cwd/b/f", "/tmp/a/b/f");

	int dir = open("/tmp/a/b", O_RDONLY | O_DIRECTORY);
	CHECK(dir >= 0);
	char path[64];
	snprintf(path, sizeof path, "/proc/self/fd/%d", dir);
	expect(path, "/tmp/a/b");
	snprintf(path, sizeof path, "/proc/self/fd/%d/f", dir);
	expect(path, "/tmp/a/b/f");
	/* The working directory, through its magic link. */
	CHECK(chdir("/proc/self/cwd/b") == 0);
	char cwd[PATH_MAX];
	CHECK(getcwd(cwd, sizeof cwd) && !strcmp(cwd, "/tmp/a/b"));

	char out[PATH_MAX];
	errno = 0;
	CHECK(!realpath("/tmp/missing/x", out) && errno == ENOENT);
	errno = 0;
	CHECK(!realpath("/tmp/a/b/f/x", out) && errno == ENOTDIR);

	if (fails)
		return 1;
	puts("PASS");
	return 0;
}
