// The new mount API in a mount namespace of the test's own: open_tree(OPEN_TREE_CLONE) copies a
// tree that move_mount attaches, beneath the top mount with MOVE_MOUNT_BENEATH (move_mount03's
// sequence); fsopen, fsconfig and fsmount make a tmpfs that move_mount attaches with its options
// and flags; statx(STATX_MNT_ID_UNIQUE), statmount and listmount describe it. Without
// CAP_SYS_ADMIN (a uid other than 0), each fails with EPERM.
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/mount.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: CHECK(%s) failed (errno %s)\n", __LINE__, #c, strerror(errno)); fails++; } } while (0)
#define FAILS(call, e) do { errno = 0; long r_ = (long)(call); if (r_ != -1 || errno != (e)) { fprintf(stderr, "line %d: %s = %ld (%s), want -1 %s\n", __LINE__, #call, r_, strerror(errno), #e); fails++; } } while (0)

// Older C libraries name them only as __NR_*; the numbers are the same everywhere.
#ifndef SYS_statmount
#define SYS_statmount 457
#endif
#ifndef SYS_listmount
#define SYS_listmount 458
#endif

#define TOP "/tmp/fs-mount-new-api"
#define TMPFS_MAGIC 0x01021994

static int open_tree_(int dfd, const char *path, unsigned flags)
{
	return syscall(SYS_open_tree, dfd, path, flags);
}

static int move_mount_(int from_dfd, const char *from, int to_dfd, const char *to, unsigned flags)
{
	return syscall(SYS_move_mount, from_dfd, from, to_dfd, to, flags);
}

static int fsopen_(const char *name, unsigned flags)
{
	return syscall(SYS_fsopen, name, flags);
}

static int fsconfig_(int fd, unsigned cmd, const char *key, const void *value, int aux)
{
	return syscall(SYS_fsconfig, fd, cmd, key, value, aux);
}

static int fsmount_(int fd, unsigned flags, unsigned attr)
{
	return syscall(SYS_fsmount, fd, flags, attr);
}

static long statmount_(uint64_t id, uint64_t mask, struct statmount *buf, size_t size)
{
	struct mnt_id_req req = { .size = MNT_ID_REQ_SIZE_VER0, .mnt_id = id, .param = mask };
	return syscall(SYS_statmount, &req, buf, size, 0);
}

static long listmount_(uint64_t id, uint64_t last, uint64_t *ids, size_t n)
{
	struct mnt_id_req req = { .size = MNT_ID_REQ_SIZE_VER0, .mnt_id = id, .param = last };
	return syscall(SYS_listmount, &req, ids, n, 0);
}

static uint64_t mnt_id(const char *path, unsigned mask)
{
	struct statx sx;
	memset(&sx, 0, sizeof sx);
	CHECK(statx(AT_FDCWD, path, 0, mask, &sx) == 0);
	CHECK(sx.stx_mask & mask);
	return sx.stx_mnt_id;
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

/// The mountinfo line of the mount at `point`, or an empty string.
static void mountinfo(const char *point, char *out, size_t size)
{
	FILE *f = fopen("/proc/self/mountinfo", "r");
	char line[1024], mnt[512];
	out[0] = 0;
	if (!f)
		return;
	while (fgets(line, sizeof line, f))
		if (sscanf(line, "%*d %*d %*d:%*d %*s %511s", mnt) == 1 && !strcmp(mnt, point))
			snprintf(out, size, "%s", line);
	fclose(f);
}

int main(void)
{
	mkdir(TOP, 0755);
	if (geteuid() != 0) {
		FAILS(open_tree_(AT_FDCWD, TOP, OPEN_TREE_CLONE), EPERM);
		FAILS(move_mount_(AT_FDCWD, TOP, AT_FDCWD, TOP, 0), EPERM);
		FAILS(fsopen_("tmpfs", 0), EPERM);
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
	mkdir(TOP "/c", 0755);
	mkdir(TOP "/m", 0755);

	// move_mount03: a copy of A goes beneath B and shows once B is unmounted.
	CHECK(mount("a", TOP "/a", "tmpfs", 0, NULL) == 0);
	CHECK(mount("b", TOP "/b", "tmpfs", 0, NULL) == 0);
	touch(TOP "/a/A");
	touch(TOP "/b/B");
	int a = open_tree_(AT_FDCWD, TOP "/a", OPEN_TREE_CLONE | OPEN_TREE_CLOEXEC);
	CHECK(a >= 0);
	int b = open(TOP "/b", O_PATH | O_NOFOLLOW);
	CHECK(b >= 0);
	CHECK(move_mount_(a, "", b, "", MOVE_MOUNT_BENEATH | MOVE_MOUNT_F_EMPTY_PATH |
					    MOVE_MOUNT_T_EMPTY_PATH) == 0);
	close(a);
	close(b);
	CHECK(exists(TOP "/b/B"));
	CHECK(exists(TOP "/a/A"));
	CHECK(!exists(TOP "/b/A"));
	CHECK(umount2(TOP "/b", 0) == 0);
	CHECK(exists(TOP "/b/A"));
	CHECK(umount2(TOP "/b", 0) == 0);

	// A recursive copy takes what is mounted below; until attached it is in no namespace.
	char line[1024];
	int copy = open_tree_(AT_FDCWD, TOP, OPEN_TREE_CLONE | AT_RECURSIVE);
	CHECK(copy >= 0);
	mountinfo(TOP "/c", line, sizeof line);
	CHECK(line[0] == 0);
	CHECK(move_mount_(copy, "", AT_FDCWD, TOP "/c", MOVE_MOUNT_F_EMPTY_PATH) == 0);
	close(copy);
	CHECK(exists(TOP "/c/a/A"));
	CHECK(umount2(TOP "/c", MNT_DETACH) == 0);
	CHECK(umount2(TOP "/a", 0) == 0);
	FAILS(open_tree_(AT_FDCWD, TOP, AT_RECURSIVE), EINVAL);

	// A filesystem context: parameters, an instance, a detached mount, attached.
	FAILS(fsopen_("no-such-fs", 0), ENODEV);
	int fs = fsopen_("tmpfs", FSOPEN_CLOEXEC);
	CHECK(fs >= 0);
	CHECK(fsconfig_(fs, FSCONFIG_SET_STRING, "source", "made", 0) == 0);
	CHECK(fsconfig_(fs, FSCONFIG_SET_STRING, "size", "1m", 0) == 0);
	FAILS(fsconfig_(fs, FSCONFIG_SET_STRING, "no-such-option", "1", 0), EINVAL);
	FAILS(fsconfig_(fs, FSCONFIG_SET_FLAG, "ro", "x", 0), EINVAL);
	FAILS(fsmount_(fs, 0, 0), EINVAL);
	CHECK(fsconfig_(fs, FSCONFIG_CMD_CREATE, NULL, NULL, 0) == 0);
	FAILS(fsconfig_(fs, FSCONFIG_CMD_CREATE, NULL, NULL, 0), EBUSY);
	int m = fsmount_(fs, FSMOUNT_CLOEXEC, MOUNT_ATTR_NODEV);
	CHECK(m >= 0);
	close(fs);
	CHECK(move_mount_(m, "", AT_FDCWD, TOP "/m", MOVE_MOUNT_F_EMPTY_PATH) == 0);
	close(m);
	mountinfo(TOP "/m", line, sizeof line);
	CHECK(strstr(line, " rw,nodev,relatime ") != NULL);
	CHECK(strstr(line, " - tmpfs made rw,size=1024k") != NULL);

	// statx's ids, and statmount and listmount by the unique one.
	uint64_t id = mnt_id(TOP "/m", STATX_MNT_ID_UNIQUE);
	uint64_t old = mnt_id(TOP "/m", STATX_MNT_ID);
	uint64_t parent = mnt_id(TOP, STATX_MNT_ID_UNIQUE);
	CHECK(id != old);
	static union {
		struct statmount sm;
		char bytes[4096];
	} buf;
	uint64_t mask = STATMOUNT_SB_BASIC | STATMOUNT_MNT_BASIC | STATMOUNT_FS_TYPE |
			STATMOUNT_MNT_ROOT | STATMOUNT_MNT_POINT;
	CHECK(statmount_(id, mask, &buf.sm, sizeof buf) == 0);
	CHECK((buf.sm.mask & mask) == mask);
	CHECK(buf.sm.mnt_id == id);
	CHECK(buf.sm.mnt_id_old == old);
	CHECK(buf.sm.mnt_parent_id == parent);
	CHECK(buf.sm.sb_magic == TMPFS_MAGIC);
	CHECK(buf.sm.mnt_attr & MOUNT_ATTR_NODEV);
	CHECK(buf.sm.mnt_propagation == MS_PRIVATE);
	CHECK(!strcmp(buf.sm.str + buf.sm.fs_type, "tmpfs"));
	CHECK(!strcmp(buf.sm.str + buf.sm.mnt_root, "/"));
	CHECK(!strcmp(buf.sm.str + buf.sm.mnt_point, TOP "/m"));
	FAILS(statmount_(id, mask, &buf.sm, sizeof buf.sm), EOVERFLOW);
	uint64_t ids[32];
	long n = listmount_(parent, 0, ids, 32);
	CHECK(n == 1);
	CHECK(n >= 1 && ids[0] == id);
	CHECK(listmount_(parent, id, ids, 32) == 0);
	CHECK(listmount_(LSMT_ROOT, 0, ids, 32) >= 2);

	CHECK(umount2(TOP "/m", 0) == 0);
	CHECK(umount2(TOP, MNT_DETACH) == 0);
	rmdir(TOP);

	if (fails)
		return 1;
	puts("PASS");
	return 0;
}
