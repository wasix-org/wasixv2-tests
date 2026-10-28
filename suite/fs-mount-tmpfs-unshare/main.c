// unshare(CLONE_NEWNS) gives the caller a copy of the mount tree: a tmpfs mounted there shows in
// its /proc/self/mountinfo with the source and flags given, and is not in the original
// namespace, which setns returns to. The new namespace's nsfs inode differs from the old one's.
// Without CAP_SYS_ADMIN (a uid other than 0), unshare, mount and umount fail with EPERM.
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

#define DIR "/tmp/fs-mount-tmpfs-unshare"

/// The /proc/self/mountinfo line of the mount at `point`, in `line`; 0 if there is none.
static int mountinfo(const char *point, char *line, size_t size)
{
	FILE *f = fopen("/proc/self/mountinfo", "r");
	if (!f)
		return 0;
	int found = 0;
	char mnt[512];
	while (fgets(line, size, f)) {
		if (sscanf(line, "%*d %*d %*d:%*d %*s %511s", mnt) == 1 && !strcmp(mnt, point)) {
			found = 1;
			break;
		}
	}
	fclose(f);
	return found;
}

static void link_of(const char *path, char *out, size_t size)
{
	ssize_t n = readlink(path, out, size - 1);
	out[n > 0 ? n : 0] = 0;
}

int main(void)
{
	char line[1024], before[64], after[64];
	mkdir(DIR, 0755);
	CHECK(access(DIR, F_OK) == 0);
	link_of("/proc/self/ns/mnt", before, sizeof before);
	CHECK(strncmp(before, "mnt:[", 5) == 0);

	if (geteuid() != 0) {
		FAILS(unshare(CLONE_NEWNS), EPERM);
		FAILS(mount("none", DIR, "tmpfs", 0, NULL), EPERM);
		FAILS(umount2("/proc", 0), EPERM);
		link_of("/proc/self/ns/mnt", after, sizeof after);
		CHECK(strcmp(before, after) == 0);
	} else {
		int old = open("/proc/self/ns/mnt", O_RDONLY);
		CHECK(old >= 0);
		CHECK(unshare(CLONE_NEWNS) == 0);
		link_of("/proc/self/ns/mnt", after, sizeof after);
		CHECK(strncmp(after, "mnt:[", 5) == 0 && strcmp(before, after) != 0);
		// Nothing below propagates back, whatever the original's mounts are.
		CHECK(mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) == 0);

		CHECK(!mountinfo(DIR, line, sizeof line));
		CHECK(mount("fs-mount-src", DIR, "tmpfs", MS_NOSUID | MS_NODEV, "mode=0700,size=1m") == 0);
		CHECK(mountinfo(DIR, line, sizeof line));
		CHECK(strstr(line, " rw,nosuid,nodev") != NULL);
		CHECK(strstr(line, " - tmpfs fs-mount-src ") != NULL);
		struct stat st;
		CHECK(stat(DIR, &st) == 0 && (st.st_mode & 07777) == 0700);
		int fd = open(DIR "/inside", O_CREAT | O_WRONLY, 0644);
		CHECK(fd >= 0);
		close(fd);
		FAILS(mount("none", DIR, "no-such-filesystem", 0, NULL), ENODEV);
		FAILS(mount("none", DIR, "tmpfs", 0, "no-such-option"), EINVAL);

		CHECK(setns(old, CLONE_NEWNS) == 0);
		link_of("/proc/self/ns/mnt", after, sizeof after);
		CHECK(strcmp(before, after) == 0);
		CHECK(!mountinfo(DIR, line, sizeof line));
		CHECK(access(DIR "/inside", F_OK) == -1 && errno == ENOENT);
		close(old);
	}
	rmdir(DIR);

	if (fails)
		return 1;
	puts("PASS");
	return 0;
}
