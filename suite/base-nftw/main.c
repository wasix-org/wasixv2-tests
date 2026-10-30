// nftw must traverse past its descriptor budget, run FTW_CHDIR callbacks in
// the containing directory, and release descriptors when a callback stops it.
//#AbstractConfig: base
//#ExpectedStdout: ok
//#Config: limit:base
//#Args: limit
//#Config: chdir:base
//#Args: chdir
//#Config: aliases:base
//#Args: aliases
//#Config: abort:base
//#Args: abort
#define _XOPEN_SOURCE 700
#include <dirent.h>
#include <fcntl.h>
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int visited, bad_cwd, stop;
static char original[4096];

static int visit(const char *path, const struct stat *st, int type, struct FTW *f)
{
	(void)st;
	visited++;
	if (type == FTW_F) {
		struct stat here;
		if (stat(path + f->base, &here)) bad_cwd = 1;
	}
	return stop;
}

#define CHECK(c) do { if (!(c)) { \
	fprintf(stderr, "line %d: %s (visited %d cwd %d)\n", __LINE__, #c, visited, bad_cwd); \
	return 1; \
} } while (0)

int main(int argc, char **argv)
{
	CHECK(argc == 2 && getcwd(original, sizeof original));
	CHECK(mkdir("tree", 0700) == 0 && mkdir("tree/sub", 0700) == 0);
	CHECK(mkdir("tree/sub/deep", 0700) == 0);
	int fd = open("tree/sub/deep/file", O_CREAT | O_WRONLY, 0600);
	CHECK(fd >= 0 && close(fd) == 0);
	if (!strcmp(argv[1], "limit")) {
		CHECK(nftw("tree", visit, 1, FTW_PHYS) == 0);
		CHECK(visited == 4);
	} else if (!strcmp(argv[1], "chdir")) {
		CHECK(nftw("tree", visit, 8, FTW_PHYS | FTW_CHDIR) == 0);
		CHECK(visited == 4 && !bad_cwd);
		char after[4096];
		CHECK(getcwd(after, sizeof after) && !strcmp(after, original));
		stop = 77;
		CHECK(nftw("tree/sub", visit, 1, FTW_PHYS | FTW_CHDIR | FTW_DEPTH) == 77);
		CHECK(getcwd(after, sizeof after) && !strcmp(after, original));
	} else if (!strcmp(argv[1], "aliases")) {
        CHECK(symlink("sub", "tree/alias") == 0 && symlink("sub", "tree/alias2") == 0);
        CHECK(nftw("tree", visit, 8, 0) == 0 && visited == 4);
        CHECK(unlink("tree/alias") == 0 && unlink("tree/alias2") == 0);
    } else {
		fd = open(".", O_RDONLY);
		CHECK(fd >= 0 && close(fd) == 0);
		stop = 77;
		for (int i = 0; i < 4; i++) CHECK(nftw("tree", visit, 8, FTW_PHYS) == 77);
		int after = open(".", O_RDONLY);
		CHECK(after == fd && close(after) == 0);
	}
	CHECK(unlink("tree/sub/deep/file") == 0);
	CHECK(rmdir("tree/sub/deep") == 0 && rmdir("tree/sub") == 0 && rmdir("tree") == 0);
	puts("ok");
	return 0;
}
