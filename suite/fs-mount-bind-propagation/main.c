// Bind mounts, moves, remounts, unmounts and propagation in a mount namespace of the test's own:
// a bind of a shared mount is its peer, so a mount on one shows on the other, and unmounting
// it unmounts both; a slave gets its master's mounts but sends nothing back, as mountinfo's
// shared:N and master:N fields say. A bind remounted read-only refuses writes while the original
// takes them; umount is EBUSY while a file on the mount is open, MNT_DETACH unmounts anyway.
// Without CAP_SYS_ADMIN (a uid other than 0), each fails with EPERM.
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

#define TOP "/tmp/fs-mount-bind-propagation"

/// The value of mountinfo's tagged field `tag` ("shared" or "master") for the mount at `point`:
/// 0 without the field, -1 without the mount.
static long tag_of(const char *point, const char *tag)
{
	FILE *f = fopen("/proc/self/mountinfo", "r");
	if (!f)
		return -1;
	char line[1024], mnt[512];
	long value = -1;
	while (fgets(line, sizeof line, f)) {
		if (sscanf(line, "%*d %*d %*d:%*d %*s %511s", mnt) != 1 || strcmp(mnt, point))
			continue;
		value = 0;
		char key[32];
		snprintf(key, sizeof key, " %s:", tag);
		char *at = strstr(line, key), *end = strstr(line, " - ");
		if (at && at < end)
			sscanf(at + strlen(key), "%ld", &value);
	}
	fclose(f);
	return value;
}

static int exists(const char *path)
{
	return access(path, F_OK) == 0;
}

static void touch(const char *path)
{
	int fd = open(path, O_CREAT | O_WRONLY, 0644);
	CHECK(fd >= 0);
	close(fd);
}

int main(void)
{
	mkdir(TOP, 0755);
	if (geteuid() != 0) {
		FAILS(mount(TOP, TOP, NULL, MS_BIND, NULL), EPERM);
		FAILS(mount(NULL, TOP, NULL, MS_SHARED, NULL), EPERM);
		FAILS(mount(NULL, "/", NULL, MS_REMOUNT | MS_RDONLY, NULL), EPERM);
		rmdir(TOP);
		if (fails)
			return 1;
		puts("PASS");
		return 0;
	}
	CHECK(unshare(CLONE_NEWNS) == 0);
	CHECK(mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) == 0);
	CHECK(mount("top", TOP, "tmpfs", 0, NULL) == 0);
	mkdir(TOP "/a", 0755);
	mkdir(TOP "/b", 0755);
	mkdir(TOP "/s", 0755);
	mkdir(TOP "/moved", 0755);

	// A shared mount and its peer.
	CHECK(mount("a", TOP "/a", "tmpfs", 0, NULL) == 0);
	CHECK(tag_of(TOP "/a", "shared") == 0);
	CHECK(mount(NULL, TOP "/a", NULL, MS_SHARED, NULL) == 0);
	long group = tag_of(TOP "/a", "shared");
	CHECK(group > 0);
	mkdir(TOP "/a/sub", 0755);
	CHECK(mount(TOP "/a", TOP "/b", NULL, MS_BIND, NULL) == 0);
	CHECK(tag_of(TOP "/b", "shared") == group);
	CHECK(mount(TOP "/a", TOP "/s", NULL, MS_BIND, NULL) == 0);
	CHECK(mount(NULL, TOP "/s", NULL, MS_SLAVE, NULL) == 0);
	CHECK(tag_of(TOP "/s", "shared") == 0);
	CHECK(tag_of(TOP "/s", "master") == group);

	// A mount on the shared mount propagates to the peer and the slave.
	CHECK(mount("sub", TOP "/a/sub", "tmpfs", 0, NULL) == 0);
	touch(TOP "/a/sub/file");
	CHECK(exists(TOP "/b/sub/file"));
	CHECK(exists(TOP "/s/sub/file"));
	// The slave's own mounts stay its own.
	mkdir(TOP "/s/own", 0755);
	CHECK(mount("own", TOP "/s/own", "tmpfs", 0, NULL) == 0);
	touch(TOP "/s/own/file");
	CHECK(!exists(TOP "/a/own/file"));
	CHECK(umount2(TOP "/s/own", 0) == 0);
	// Unmounting the original unmounts the copies.
	CHECK(umount2(TOP "/a/sub", 0) == 0);
	CHECK(!exists(TOP "/b/sub/file"));
	CHECK(!exists(TOP "/s/sub/file"));

	// A read-only bind; the original stays writable.
	CHECK(mount(NULL, TOP "/b", NULL, MS_REMOUNT | MS_BIND | MS_RDONLY, NULL) == 0);
	FAILS(open(TOP "/b/ro", O_CREAT | O_WRONLY, 0644), EROFS);
	touch(TOP "/a/rw");
	CHECK(exists(TOP "/b/rw"));

	// Moving needs a parent that does not propagate.
	FAILS(mount(TOP "/a/sub", TOP "/moved", NULL, MS_MOVE, NULL), EINVAL);
	CHECK(mount(TOP "/s", TOP "/moved", NULL, MS_MOVE, NULL) == 0);
	CHECK(tag_of(TOP "/s", "master") == -1);
	CHECK(tag_of(TOP "/moved", "master") == group);

	// Busy while used.
	int fd = open(TOP "/b/rw", O_RDONLY);
	CHECK(fd >= 0);
	FAILS(umount2(TOP "/b", 0), EBUSY);
	CHECK(umount2(TOP "/b", MNT_DETACH) == 0);
	CHECK(tag_of(TOP "/b", "shared") == -1);
	char byte;
	CHECK(read(fd, &byte, 1) == 0);
	close(fd);
	FAILS(umount2(TOP "/a/sub", 0), EINVAL);
	FAILS(umount2(TOP, 0), EBUSY);
	CHECK(umount2(TOP, MNT_DETACH) == 0);
	rmdir(TOP);

	if (fails)
		return 1;
	puts("PASS");
	return 0;
}
