// /proc/self/ns: a link per namespace kind, named kind:[inode] as nsfs names the namespace's
// inode, with pid_for_children and time_for_children naming pid and time namespaces. An open
// link is the namespace: NS_GET_NSTYPE gives its CLONE_NEW* flag, NS_GET_USERNS a descriptor for
// the user namespace that owns it, and setns enters it (with CAP_SYS_ADMIN). unshare of what
// nothing shares succeeds.
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef NS_GET_USERNS
#define NS_GET_USERNS 0xb701
#define NS_GET_PARENT 0xb702
#define NS_GET_NSTYPE 0xb703
#define NS_GET_OWNER_UID 0xb704
#endif
#ifndef CLONE_NEWTIME
#define CLONE_NEWTIME 0x80
#endif

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: CHECK(%s) failed (errno %s)\n", __LINE__, #c, strerror(errno)); fails++; } } while (0)
#define FAILS(call, e) do { errno = 0; long r_ = (long)(call); if (r_ != -1 || errno != (e)) { fprintf(stderr, "line %d: %s = %ld (%s), want -1 %s\n", __LINE__, #call, r_, strerror(errno), #e); fails++; } } while (0)

/// The kind and inode number /proc/self/ns/<name> names; 0 when it does not read as kind:[ino].
static unsigned long link_of(const char *name, char *kind, size_t size)
{
	char path[64], target[64];
	snprintf(path, sizeof path, "/proc/self/ns/%s", name);
	ssize_t n = readlink(path, target, sizeof target - 1);
	if (n < 0)
		return 0;
	target[n] = 0;
	char *open = strchr(target, ':');
	unsigned long ino = 0;
	if (!open || sscanf(open, ":[%lu]", &ino) != 1 || (size_t)(open - target) >= size)
		return 0;
	memcpy(kind, target, open - target);
	kind[open - target] = 0;
	return ino;
}

int main(void)
{
	static const char *const links[][2] = {
		{"net", "net"}, {"uts", "uts"}, {"ipc", "ipc"}, {"pid", "pid"},
		{"pid_for_children", "pid"}, {"user", "user"}, {"mnt", "mnt"},
		{"cgroup", "cgroup"}, {"time", "time"}, {"time_for_children", "time"},
	};
	char kind[32];
	for (size_t i = 0; i < sizeof links / sizeof *links; i++) {
		unsigned long ino = link_of(links[i][0], kind, sizeof kind);
		if (!ino || strcmp(kind, links[i][1])) {
			fprintf(stderr, "%s: %s:[%lu]\n", links[i][0], kind, ino);
			fails++;
		}
	}
	char pid_kind[32];
	CHECK(link_of("pid", kind, sizeof kind) == link_of("pid_for_children", pid_kind, sizeof pid_kind));

	// The link leads to the namespace's inode: a regular file all may read.
	unsigned long uts_ino = link_of("uts", kind, sizeof kind);
	struct stat st;
	CHECK(stat("/proc/self/ns/uts", &st) == 0);
	CHECK(st.st_ino == uts_ino);
	CHECK(S_ISREG(st.st_mode) && (st.st_mode & 07777) == 0444);

	int uts = open("/proc/self/ns/uts", O_RDONLY);
	CHECK(uts >= 0);
	CHECK(fstat(uts, &st) == 0 && st.st_ino == uts_ino);
	char c;
	FAILS(read(uts, &c, 1), EINVAL);
	CHECK(ioctl(uts, NS_GET_NSTYPE) == CLONE_NEWUTS);
	FAILS(ioctl(uts, NS_GET_PARENT), EINVAL);
	unsigned owner;
	FAILS(ioctl(uts, NS_GET_OWNER_UID, &owner), EINVAL);

	// The user namespace owning the UTS namespace is the caller's own.
	int user = ioctl(uts, NS_GET_USERNS);
	CHECK(user >= 0);
	CHECK(fcntl(user, F_GETFD) == FD_CLOEXEC);
	CHECK(ioctl(user, NS_GET_NSTYPE) == CLONE_NEWUSER);
	CHECK(fstat(user, &st) == 0 && st.st_ino == link_of("user", kind, sizeof kind));
	CHECK(ioctl(user, NS_GET_OWNER_UID, &owner) == 0);

	int time = open("/proc/self/ns/time_for_children", O_RDONLY);
	CHECK(time >= 0);
	CHECK(ioctl(time, NS_GET_NSTYPE) == CLONE_NEWTIME);

	// setns: the kind must match; entering needs CAP_SYS_ADMIN, which a process without a uid
	// of 0 lacks; the own user namespace cannot be entered again.
	FAILS(setns(uts, CLONE_NEWNET), EINVAL);
	errno = 0;
	int entered = setns(uts, 0);
	CHECK(entered == 0 || errno == EPERM);
	if (geteuid() != 0)
		CHECK(entered == -1 && errno == EPERM);
	int own = open("/proc/self/ns/user", O_RDONLY);
	FAILS(setns(own, CLONE_NEWUSER), EINVAL);
	int p[2];
	CHECK(pipe(p) == 0);
	FAILS(setns(p[0], 0), EINVAL);
	FAILS(setns(-1, 0), EBADF);

	// unshare of what nothing shares, and of an unknown flag.
	CHECK(unshare(0) == 0);
	CHECK(unshare(CLONE_FILES | CLONE_FS | CLONE_SYSVSEM) == 0);
	FAILS(unshare(CLONE_SETTLS), EINVAL);

	if (fails)
		return 1;
	puts("PASS");
	return 0;
}
