// Unmounting trees with propagation, as LTP's fs_bind01 does with mount(8): shared directories
// bound on themselves, binds between them and their copies, then each unmounted in turn, all
// succeed and leave nothing mounted. Also: a remount read-only is EBUSY while a file is open for
// writing, and mountinfo shows a tmpfs's size and mode options.
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: CHECK(%s) failed (errno %s)\n", __LINE__, #c, strerror(errno)); fails++; } } while (0)
#define FAILS(call, e) do { errno = 0; long r_ = (long)(call); if (r_ != -1 || errno != (e)) { fprintf(stderr, "line %d: %s = %ld (%s), want -1 %s\n", __LINE__, #call, r_, strerror(errno), #e); fails++; } } while (0)

#define TOP "/tmp/fs-mount-umount-tree"

/// `fs_bind_makedir rshared dir`.
static void rshared(const char *dir)
{
	CHECK(mkdir(dir, 0755) == 0);
	CHECK(mount(dir, dir, NULL, MS_BIND, NULL) == 0);
	CHECK(mount(NULL, dir, NULL, MS_REC | MS_SHARED, NULL) == 0);
}

static void bind(const char *from, const char *to)
{
	CHECK(mount(from, to, NULL, MS_BIND, NULL) == 0);
}

static void umount_ok(const char *at)
{
	if (umount2(at, 0) != 0) {
		fprintf(stderr, "umount %s: %s\n", at, strerror(errno));
		fails++;
	}
}

/// The mountinfo lines of mounts strictly below `top`, and the line of `point` into `line`.
static int mounts_below(const char *top, const char *point, char *found, size_t size)
{
	FILE *f = fopen("/proc/self/mountinfo", "r");
	char line[1024], mnt[512];
	size_t len = strlen(top);
	int n = 0;
	if (found)
		found[0] = 0;
	if (!f)
		return -1;
	while (fgets(line, sizeof line, f)) {
		if (sscanf(line, "%*d %*d %*d:%*d %*s %511s", mnt) != 1)
			continue;
		if (!strncmp(mnt, top, len) && mnt[len] == '/')
			n++;
		if (point && found && !strcmp(mnt, point))
			snprintf(found, size, "%s", line);
	}
	fclose(f);
	return n;
}

int main(void)
{
	if (geteuid() != 0) {
		puts("PASS");
		return 0;
	}
	mkdir(TOP, 0755);
	CHECK(unshare(CLONE_NEWNS) == 0);
	CHECK(mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) == 0);
	CHECK(mount("top", TOP, "tmpfs", 0, NULL) == 0);
	CHECK(chdir(TOP) == 0);
	const char *dirs[] = { "disk1", "disk1/a", "disk1/b", "disk2", "disk3" };
	for (unsigned i = 0; i < sizeof dirs / sizeof *dirs; i++)
		CHECK(mkdir(dirs[i], 0755) == 0);

	// fs_bind01: shared child to shared parent.
	rshared("parent1");
	rshared("parent2");
	rshared("share2");
	rshared("parent1/child1");
	bind("disk1", "parent1/child1");
	CHECK(mkdir("parent2/child2", 0755) == 0);
	bind("parent2", "share2");
	bind("parent1/child1", "parent2/child2");
	bind("disk2", "parent1/child1/a");
	CHECK(access("share2/child2/a", F_OK) == 0);
	bind("disk3", "parent2/child2/b");
	CHECK(access("parent1/child1/b", F_OK) == 0);
	const char *order[] = {
		"parent1/child1/a", "parent1/child1/b", "parent2/child2", "parent1/child1",
		"parent1/child1", "share2", "share2", "parent2", "parent1",
	};
	for (unsigned i = 0; i < sizeof order / sizeof *order; i++)
		umount_ok(order[i]);
	CHECK(mounts_below(TOP, NULL, NULL, 0) == 0);

	// A file open for writing keeps the filesystem writable.
	CHECK(mkdir("w", 0755) == 0);
	CHECK(mount("w", TOP "/w", "tmpfs", 0, NULL) == 0);
	int fd = open("w/f", O_CREAT | O_WRONLY, 0644);
	CHECK(fd >= 0);
	FAILS(mount(NULL, TOP "/w", NULL, MS_REMOUNT | MS_RDONLY, NULL), EBUSY);
	FAILS(mount(NULL, TOP "/w", NULL, MS_REMOUNT | MS_BIND | MS_RDONLY, NULL), EBUSY);
	close(fd);
	CHECK(mount(NULL, TOP "/w", NULL, MS_REMOUNT | MS_RDONLY, NULL) == 0);
	FAILS(open("w/g", O_CREAT | O_WRONLY, 0644), EROFS);
	umount_ok(TOP "/w");

	// tmpfs's options in mountinfo.
	CHECK(mkdir("o", 0755) == 0);
	CHECK(mount("o", TOP "/o", "tmpfs", 0, "size=1m,mode=700") == 0);
	char line[1024];
	mounts_below(TOP, TOP "/o", line, sizeof line);
	CHECK(strstr(line, " - tmpfs o rw,size=1024k,") != NULL);
	CHECK(strstr(line, ",mode=700") != NULL);
	umount_ok(TOP "/o");

	CHECK(chdir("/") == 0);
	CHECK(umount2(TOP, MNT_DETACH) == 0);
	rmdir(TOP);
	if (fails)
		return 1;
	puts("PASS");
	return 0;
}
