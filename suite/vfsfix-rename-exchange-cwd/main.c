// RENAME_EXCHANGE of the working directory: it moves with its new name, getcwd reports it,
// and creating in it works, as both names stay live (d_exchange).
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

int main(void)
{
	char cwd[64];
	struct stat st;

	OK(mkdir("/tmp/a", 0755));
	OK(mkdir("/tmp/b", 0755));
	OK(chdir("/tmp/a"));
	int dir = open("/tmp/b", O_RDONLY | O_DIRECTORY);
	CHECK(dir >= 0);

	OK(renameat2(AT_FDCWD, "/tmp/a", AT_FDCWD, "/tmp/b", RENAME_EXCHANGE));
	CHECK(getcwd(cwd, sizeof cwd) && !strcmp(cwd, "/tmp/b"));
	OK(mkdir("x", 0755));
	OK(stat("/tmp/b/x", &st));
	/* The other directory, held open, is live under the other name. */
	OK(mkdirat(dir, "y", 0755));
	OK(stat("/tmp/a/y", &st));

	/* And back. */
	OK(renameat2(AT_FDCWD, "/tmp/a", AT_FDCWD, "/tmp/b", RENAME_EXCHANGE));
	CHECK(getcwd(cwd, sizeof cwd) && !strcmp(cwd, "/tmp/a"));
	OK(stat("x", &st));

	/* A plain rename of the working directory. */
	OK(rename("/tmp/a", "/tmp/c"));
	CHECK(getcwd(cwd, sizeof cwd) && !strcmp(cwd, "/tmp/c"));
	OK(mkdir("z", 0755));

	close(dir);
	if (fails)
		return 1;
	puts("PASS");
	return 0;
}
