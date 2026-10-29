// `unshare -Ur` by hand: any user makes a user namespace, where it starts as the overflow uid
// with every capability, maps its own uid and gid to root (setgroups denied first, each map
// written once), and is root there: what it creates is its own uid's outside. The
// capabilities count only in the namespace: no device nodes, no hard limit raised, no
// machine tunable written, no supplementary groups set.
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/wait.h>
#include <unistd.h>

/// `NS_GET_OWNER_UID` (`linux/nsfs.h`).
#define NS_GET_OWNER_UID _IO(0xb7, 0x4)

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: CHECK(%s) failed (errno %s)\n", __LINE__, #c, strerror(errno)); fails++; } } while (0)
#define FAILS(call, e) do { errno = 0; long r_ = (long)(call); if (r_ != -1 || errno != (e)) { fprintf(stderr, "line %d: %s = %ld (%s), want -1 %s\n", __LINE__, #call, r_, strerror(errno), #e); fails++; } } while (0)

/// Writes `text` to `path` in one write: the bytes written, or -1.
static long write_file(const char *path, const char *text)
{
	int fd = open(path, O_WRONLY);
	if (fd < 0)
		return -1;
	long n = write(fd, text, strlen(text));
	int saved = errno;
	close(fd);
	errno = saved;
	return n;
}

/// The value of `key` in /proc/self/status.
static void status(const char *key, char *out, size_t size)
{
	char buf[4096];
	out[0] = 0;
	int fd = open("/proc/self/status", O_RDONLY);
	if (fd < 0)
		return;
	ssize_t n = read(fd, buf, sizeof buf - 1);
	close(fd);
	if (n <= 0)
		return;
	buf[n] = 0;
	char *line = strstr(buf, key);
	if (!line)
		return;
	line += strlen(key) + 2;
	snprintf(out, size, "%.*s", (int)strcspn(line, "\n"), line);
}

static void inside(uid_t uid, gid_t gid, const char *file)
{
	char text[64];
	CHECK(unshare(CLONE_NEWUSER) == 0);
	CHECK(getuid() == 65534 && getgid() == 65534);
	status("CapEff", text, sizeof text);
	CHECK(strcmp(text, "000001ffffffffff") == 0);

	CHECK(write_file("/proc/self/setgroups", "deny") == 4);
	snprintf(text, sizeof text, "0 %u 1", uid);
	CHECK(write_file("/proc/self/uid_map", text) == (long)strlen(text));
	FAILS(write_file("/proc/self/uid_map", text), EPERM);
	snprintf(text, sizeof text, "0 %u 1", gid);
	CHECK(write_file("/proc/self/gid_map", text) == (long)strlen(text));
	CHECK(getuid() == 0 && getgid() == 0);

	int ns = open("/proc/self/ns/user", O_RDONLY);
	uid_t owner = 1234;
	CHECK(ns >= 0 && ioctl(ns, NS_GET_OWNER_UID, &owner) == 0 && owner == 0);

	int fd = open(file, O_CREAT | O_WRONLY | O_EXCL, 0644);
	CHECK(fd >= 0);
	close(fd);
	struct stat st;
	CHECK(stat(file, &st) == 0 && st.st_uid == 0 && st.st_gid == 0);
	CHECK(stat("/proc/1", &st) != 0 || st.st_uid == 65534 || uid == 0);

	FAILS(mknod("/tmp/userns-null", S_IFCHR | 0666, makedev(1, 3)), EPERM);
	FAILS(setgroups(0, NULL), EPERM);
	FAILS(open("/proc/sys/kernel/pid_max", O_WRONLY), EACCES);
	struct rlimit limit;
	CHECK(getrlimit(RLIMIT_NOFILE, &limit) == 0);
	if (limit.rlim_max != RLIM_INFINITY) {
		limit.rlim_max++;
		FAILS(setrlimit(RLIMIT_NOFILE, &limit), EPERM);
	}
	exit(fails ? 1 : 0);
}

int main(void)
{
	char file[64];
	snprintf(file, sizeof file, "/tmp/userns-root-%d", getpid());
	uid_t uid = getuid();
	gid_t gid = getgid();
	pid_t child = fork();
	CHECK(child >= 0);
	if (child == 0)
		inside(uid, gid, file);
	int status = -1;
	CHECK(waitpid(child, &status, 0) == child);
	CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
	// Outside, the file is the uid that is root inside.
	struct stat st;
	CHECK(stat(file, &st) == 0 && st.st_uid == uid && st.st_gid == gid);
	unlink(file);
	printf(fails ? "FAIL\n" : "PASS\n");
	return fails ? 1 : 0;
}
