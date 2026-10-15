// A host directory mapped into the guest: reading a prefilled file, writing a file the host
// checks afterwards, and the namespace operations on the host filesystem.
//#PrefilledFile: in.txt:hello from the host
//#MappedDirectory: .:/data
//#ExpectedFile: out.txt:written by the guest
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

int main(void)
{
	char buf[64] = { 0 };
	struct stat st;

	int fd = open("/data/in.txt", O_RDONLY);
	CHECK(fd >= 0);
	CHECK(read(fd, buf, sizeof buf) == 19 && !strcmp(buf, "hello from the host"));
	close(fd);
	OK(stat("/data/in.txt", &st));
	CHECK(S_ISREG(st.st_mode) && st.st_size == 19 && st.st_uid == getuid());

	fd = open("/data/out.txt", O_CREAT | O_TRUNC | O_WRONLY, 0644);
	CHECK(fd >= 0);
	CHECK(write(fd, "written by the guest", 20) == 20);
	close(fd);

	OK(mkdir("/data/sub", 0755));
	fd = open("/data/sub/a", O_CREAT | O_RDWR, 0644);
	CHECK(write(fd, "abc", 3) == 3);
	OK(rename("/data/sub/a", "/data/sub/b"));
	OK(fstat(fd, &st));
	CHECK(st.st_size == 3);
	close(fd);
	OK(symlink("b", "/data/sub/l"));
	CHECK(readlink("/data/sub/l", buf, sizeof buf) == 1);
	OK(link("/data/sub/b", "/data/sub/c"));
	OK(stat("/data/sub/c", &st));
	CHECK(st.st_nlink == 2);
	FAILS(rmdir("/data/sub"), ENOTEMPTY);

	DIR *d = opendir("/data/sub");
	int n = 0;
	struct dirent *e;
	while ((e = readdir(d)))
		n++;
	closedir(d);
	CHECK(n == 5);

	OK(unlink("/data/sub/b"));
	OK(unlink("/data/sub/c"));
	OK(unlink("/data/sub/l"));
	OK(rmdir("/data/sub"));
	FAILS(stat("/data/sub", &st), ENOENT);

	/* ".." from the mount root leaves the mount. */
	OK(stat("/data/..", &st));
	struct stat root;
	OK(stat("/", &root));
	CHECK(st.st_ino == root.st_ino);

	if (fails)
		return 1;
	puts("PASS");
	return 0;
}
