// `unshare -Urm` by hand: a user namespace with a mount namespace it owns, where its root
// mounts a tmpfs, and, with a pid namespace too, a /proc of its own. The mounts it got from
// its parent's namespace stay over what they hide (umount is EINVAL) and keep their flags
// (clearing /proc's nosuid, nodev, noexec is EPERM). Without the mount namespace, mounting is
// EPERM: the initial one is not its namespace's.
//#BuildEnv: WASIXCC_WASM_EXCEPTIONS=no
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: CHECK(%s) failed (errno %s)\n", __LINE__, #c, strerror(errno)); fails++; } } while (0)
#define FAILS(call, e) do { errno = 0; long r_ = (long)(call); if (r_ != -1 || errno != (e)) { fprintf(stderr, "line %d: %s = %ld (%s), want -1 %s\n", __LINE__, #call, r_, strerror(errno), #e); fails++; } } while (0)

static long write_file(const char *path, const char *text)
{
	int fd = open(path, O_WRONLY);
	if (fd < 0)
		return -1;
	long n = write(fd, text, strlen(text));
	close(fd);
	return n;
}

/// `unshare(CLONE_NEWUSER | flags)` and root mapped to the caller's ids.
static void become_root(int flags)
{
	char map[32];
	uid_t uid = getuid();
	gid_t gid = getgid();
	CHECK(unshare(CLONE_NEWUSER | flags) == 0);
	CHECK(write_file("/proc/self/setgroups", "deny") == 4);
	snprintf(map, sizeof map, "0 %u 1", uid);
	CHECK(write_file("/proc/self/uid_map", map) > 0);
	snprintf(map, sizeof map, "0 %u 1", gid);
	CHECK(write_file("/proc/self/gid_map", map) > 0);
}

/// Runs `body` in a child, which reports its failures by its exit status.
static void in_child(void (*body)(void))
{
	pid_t child = fork();
	CHECK(child >= 0);
	if (child == 0) {
		body();
		exit(fails ? 1 : 0);
	}
	int status = -1;
	CHECK(waitpid(child, &status, 0) == child);
	CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

static void without_a_mount_namespace(void)
{
	become_root(0);
	FAILS(mount("t", "/tmp", "tmpfs", 0, NULL), EPERM);
}

static void proc_init(void)
{
	// The pid namespace's init: a /proc of its own shows it as pid 1.
	CHECK(getpid() == 1);
	CHECK(mount("proc", "/proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) == 0);
	char link[16] = {0};
	CHECK(readlink("/proc/self", link, sizeof link - 1) > 0 && strcmp(link, "1") == 0);
}

static void with_a_mount_namespace(void)
{
	become_root(CLONE_NEWNS | CLONE_NEWPID);
	CHECK(mount("none", "/", NULL, MS_REC | MS_PRIVATE, NULL) == 0);
	CHECK(mount("t", "/tmp", "tmpfs", 0, NULL) == 0);
	struct stat st;
	CHECK(stat("/tmp", &st) == 0 && st.st_uid == 0 && st.st_gid == 0);
	int fd = open("/tmp/inside", O_CREAT | O_WRONLY, 0644);
	CHECK(fd >= 0);
	close(fd);
	CHECK(umount("/tmp") == 0);
	CHECK(access("/tmp/inside", F_OK) != 0);

	FAILS(umount("/proc"), EINVAL);
	FAILS(mount(NULL, "/proc", NULL, MS_REMOUNT | MS_BIND, NULL), EPERM);
	CHECK(mount(NULL, "/proc", NULL, MS_REMOUNT | MS_BIND | MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) == 0);
	in_child(proc_init);
}

int main(void)
{
	in_child(without_a_mount_namespace);
	in_child(with_a_mount_namespace);
	printf(fails ? "FAIL\n" : "PASS\n");
	return fails ? 1 : 0;
}
