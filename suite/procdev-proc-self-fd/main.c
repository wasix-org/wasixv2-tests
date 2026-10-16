// /proc/self/fd: links named like Linux's (a path, pipe:[ino], anon_inode:[eventfd]), the
// directory listing every open descriptor, fdinfo, and opening a link reopening the object:
// a pipe's write end through its link, and /dev/stdout.
//#ExpectedStdout: to stdout
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: CHECK(%s) failed (errno %s)\n", __LINE__, #c, strerror(errno)); fails++; } } while (0)
#define OK(call) do { errno = 0; if ((long)(call) < 0) { fprintf(stderr, "line %d: %s failed: %s\n", __LINE__, #call, strerror(errno)); fails++; } } while (0)
#define FAILS(call, e) do { errno = 0; long r_ = (long)(call); if (r_ != -1 || errno != (e)) { fprintf(stderr, "line %d: %s = %ld (%s), want -1 %s\n", __LINE__, #call, r_, strerror(errno), #e); fails++; } } while (0)

static const char *link_of(int fd)
{
	static char target[256];
	char name[64];
	snprintf(name, sizeof name, "/proc/self/fd/%d", fd);
	ssize_t n = readlink(name, target, sizeof target - 1);
	if (n < 0)
		return "(error)";
	target[n] = 0;
	return target;
}

static int listed(int fd)
{
	DIR *dir = opendir("/proc/self/fd");
	if (!dir)
		return 0;
	struct dirent *e;
	int found = 0;
	while ((e = readdir(dir)))
		if (atoi(e->d_name) == fd && e->d_name[0] != '.')
			found = 1;
	closedir(dir);
	return found;
}

int main(void)
{
	struct stat st;
	char buf[64];

	int file = open("/tmp/file", O_CREAT | O_RDWR, 0644);
	CHECK(file >= 0);
	CHECK(!strcmp(link_of(file), "/tmp/file"));
	CHECK(listed(file));

	int p[2];
	OK(pipe(p));
	OK(fstat(p[0], &st));
	snprintf(buf, sizeof buf, "pipe:[%llu]", (unsigned long long)st.st_ino);
	CHECK(!strcmp(link_of(p[0]), buf));
	CHECK(!strcmp(link_of(p[1]), buf));
	CHECK(S_ISFIFO(st.st_mode));

	int ev = eventfd(0, 0);
	CHECK(ev >= 0);
	CHECK(!strcmp(link_of(ev), "anon_inode:[eventfd]"));
	OK(fstat(ev, &st));
	CHECK((st.st_mode & S_IFMT) == 0);

	/* Opening the write end's link attaches another writer to the same pipe. */
	snprintf(buf, sizeof buf, "/proc/self/fd/%d", p[1]);
	int w = open(buf, O_WRONLY);
	CHECK(w >= 0);
	CHECK(write(w, "again", 5) == 5);
	close(w);
	close(p[1]);
	CHECK(read(p[0], buf, sizeof buf) == 5 && !memcmp(buf, "again", 5));
	CHECK(read(p[0], buf, sizeof buf) == 0);

	/* fdinfo shows the position and the flags. */
	CHECK(write(file, "12345", 5) == 5);
	snprintf(buf, sizeof buf, "/proc/self/fdinfo/%d", file);
	FILE *info = fopen(buf, "r");
	CHECK(info != NULL);
	if (info) {
		char line[128];
		CHECK(fgets(line, sizeof line, info) && !strcmp(line, "pos:\t5\n"));
		CHECK(fgets(line, sizeof line, info) && !strncmp(line, "flags:\t", 7));
		fclose(info);
	}

	/* An unlinked file's link says so. */
	OK(unlink("/tmp/file"));
	CHECK(!strcmp(link_of(file), "/tmp/file (deleted)"));
	close(file);
	FAILS(readlink("/proc/self/fd/999", buf, sizeof buf), ENOENT);

	/* /dev/stdout is /proc/self/fd/1 and reaches the same stream. */
	int out = open("/dev/stdout", O_WRONLY);
	CHECK(out >= 0);
	CHECK(write(out, "to stdout\n", 10) == 10);
	close(out);
	CHECK(readlink("/dev/fd", buf, sizeof buf) == 13 && !memcmp(buf, "/proc/self/fd", 13));

	if (fails)
		return 1;
	puts("PASS");
	return 0;
}
