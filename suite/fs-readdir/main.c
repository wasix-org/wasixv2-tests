// opendir/readdir/rewinddir/telldir/seekdir and getcwd/chdir/fchdir.
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <dirent.h>
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

#define N 50

int main(void)
{
	char name[32];
	OK(mkdir("/tmp/d", 0755));
	for (int i = 0; i < N; i++) {
		snprintf(name, sizeof name, "/tmp/d/f%02d", i);
		close(open(name, O_CREAT | O_WRONLY, 0644));
	}
	OK(mkdir("/tmp/d/sub", 0755));
	OK(symlink("f00", "/tmp/d/link"));

	DIR *dir = opendir("/tmp/d");
	CHECK(dir != NULL);
	int seen[N] = { 0 }, dots = 0, total = 0, sub = 0, link = 0;
	long pos10 = -1;
	char at10[256] = "";
	struct dirent *e;
	while ((e = readdir(dir))) {
		total++;
		if (total == 10) {
			pos10 = telldir(dir);
		}
		if (total == 11)
			strcpy(at10, e->d_name);
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) {
			dots++;
			CHECK(e->d_type == DT_DIR);
		} else if (!strcmp(e->d_name, "sub")) {
			sub++;
			CHECK(e->d_type == DT_DIR);
		} else if (!strcmp(e->d_name, "link")) {
			link++;
			CHECK(e->d_type == DT_LNK);
		} else {
			int i = -1;
			sscanf(e->d_name, "f%d", &i);
			CHECK(i >= 0 && i < N);
			if (i >= 0 && i < N)
				seen[i]++;
			CHECK(e->d_type == DT_REG);
		}
	}
	CHECK(total == N + 4 && dots == 2 && sub == 1 && link == 1);
	for (int i = 0; i < N; i++)
		CHECK(seen[i] == 1);

	/* seekdir back to a telldir position resumes there. */
	seekdir(dir, pos10);
	e = readdir(dir);
	CHECK(e && !strcmp(e->d_name, at10));

	/* rewinddir sees new entries. */
	close(open("/tmp/d/new", O_CREAT | O_WRONLY, 0644));
	rewinddir(dir);
	int found = 0;
	total = 0;
	while ((e = readdir(dir))) {
		total++;
		found += !strcmp(e->d_name, "new");
	}
	CHECK(found == 1 && total == N + 5);
	closedir(dir);

	errno = 0;
	CHECK(opendir("/tmp/d/f00") == NULL && errno == ENOTDIR);
	errno = 0;
	CHECK(opendir("/tmp/none") == NULL && errno == ENOENT);

	/* getcwd / chdir / fchdir. */
	char cwd[256];
	CHECK(getcwd(cwd, sizeof cwd) && !strcmp(cwd, "/"));
	OK(chdir("/tmp/d/sub"));
	CHECK(getcwd(cwd, sizeof cwd) && !strcmp(cwd, "/tmp/d/sub"));
	OK(chdir(".."));
	CHECK(getcwd(cwd, sizeof cwd) && !strcmp(cwd, "/tmp/d"));
	int fd = open("f01", O_RDONLY);
	CHECK(fd >= 0);
	close(fd);
	FAILS(chdir("f01"), ENOTDIR);
	FAILS(chdir("nope"), ENOENT);
	errno = 0;
	CHECK(getcwd(cwd, 3) == NULL && errno == ERANGE);
	int dfd = open("/tmp", O_RDONLY | O_DIRECTORY);
	OK(fchdir(dfd));
	CHECK(getcwd(cwd, sizeof cwd) && !strcmp(cwd, "/tmp"));
	close(dfd);
	fd = open("/tmp/d/f01", O_RDONLY);
	FAILS(fchdir(fd), ENOTDIR);
	close(fd);
	/* A renamed cwd keeps its new name. */
	OK(chdir("/tmp/d/sub"));
	OK(rename("/tmp/d", "/tmp/e"));
	CHECK(getcwd(cwd, sizeof cwd) && !strcmp(cwd, "/tmp/e/sub"));

	if (fails)
		return 1;
	puts("PASS");
	return 0;
}
