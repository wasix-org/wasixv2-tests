// Process identity for an unprivileged process: pids, uids and gids (wasixv2 runs as uid and
// gid 1), supplementary groups, what the set*id family may and may not change, and sessions
// and process groups.
//#ExpectedStdout: ok
#define _GNU_SOURCE
#include <errno.h>
#include <grp.h>
#include <stdio.h>
#include <string.h>
#include <sys/fsuid.h>
#include <sys/types.h>
#include <unistd.h>

static int failures;
#define CHECK(cond)                                                                       \
	do {                                                                              \
		if (!(cond)) {                                                            \
			printf("FAIL line %d: %s (errno %d: %s)\n", __LINE__, #cond, errno, \
			       strerror(errno));                                          \
			failures++;                                                       \
		}                                                                         \
	} while (0)
#define CHECK_ERR(expr, err)                                                              \
	do {                                                                              \
		errno = 0;                                                                \
		long r_ = (long)(expr);                                                   \
		if (r_ != -1 || errno != (err)) {                                         \
			printf("FAIL line %d: %s = %ld errno %d, want %s\n", __LINE__, #expr, \
			       r_, errno, #err);                                          \
			failures++;                                                       \
		}                                                                         \
	} while (0)

#define NO_SUCH_PID 99999

int main(void)
{
	pid_t pid = getpid();
	CHECK(pid > 0);
	CHECK(gettid() == pid);
	CHECK(getppid() >= 0 && getppid() != pid);

	uid_t uid = getuid();
	gid_t gid = getgid();
	CHECK(uid != 0 && gid != 0);
	CHECK(geteuid() == uid && getegid() == gid);
#ifdef __wasixv2__
	CHECK(uid == 1 && gid == 1);
#endif
	uid_t r, e, s;
	CHECK(getresuid(&r, &e, &s) == 0 && r == uid && e == uid && s == uid);
	gid_t rg, eg, sg;
	CHECK(getresgid(&rg, &eg, &sg) == 0 && rg == gid && eg == gid && sg == gid);

	int n = getgroups(0, NULL);
	CHECK(n >= 0);
	gid_t groups[256];
	if (n >= 0 && n <= 256)
		CHECK(getgroups(n, groups) == n);
	CHECK_ERR(getgroups(-1, groups), EINVAL);
	if (n >= 2)
		CHECK_ERR(getgroups(1, groups), EINVAL);

	/* Unprivileged: only changes among the current ids are allowed. */
	CHECK(setuid(uid) == 0);
	CHECK(setgid(gid) == 0);
	CHECK(seteuid(uid) == 0);
	CHECK(setegid(gid) == 0);
	CHECK(setreuid(-1, -1) == 0);
	CHECK(setregid(-1, -1) == 0);
	CHECK(setresuid(-1, -1, -1) == 0);
	CHECK(setresuid(uid, uid, uid) == 0);
	CHECK(setresgid(gid, gid, gid) == 0);
	CHECK_ERR(setuid(0), EPERM);
	CHECK_ERR(setgid(0), EPERM);
	CHECK_ERR(seteuid(0), EPERM);
	CHECK_ERR(setegid(0), EPERM);
	CHECK_ERR(setreuid(0, -1), EPERM);
	CHECK_ERR(setresuid(-1, 0, -1), EPERM);
	CHECK_ERR(setresgid(-1, -1, 0), EPERM);
	CHECK_ERR(setgroups(0, NULL), EPERM);
	CHECK(getuid() == uid && geteuid() == uid && getgid() == gid);

	/* setfsuid returns the old id and changes nothing it may not. */
	CHECK(setfsuid(0) == (int)uid);
	CHECK(setfsuid(-1) == (int)uid);
	CHECK(setfsgid(0) == (int)gid);
	CHECK(setfsgid(-1) == (int)gid);

	/* Sessions and process groups. */
	pid_t pgid = getpgid(0);
	pid_t sid = getsid(0);
	CHECK(pgid > 0 && sid > 0);
	CHECK(getpgrp() == pgid);
	CHECK(getpgid(pid) == pgid && getsid(pid) == sid);
	CHECK_ERR(getpgid(NO_SUCH_PID), ESRCH);
	CHECK_ERR(getsid(NO_SUCH_PID), ESRCH);
	CHECK_ERR(setpgid(NO_SUCH_PID, 0), ESRCH);
	CHECK_ERR(setpgid(0, -1), EINVAL);
	if (sid == pid) {
		/* A session leader can neither start a new session nor change its group. */
		CHECK_ERR(setsid(), EPERM);
		CHECK_ERR(setpgid(0, 0), EPERM);
	} else if (pgid == pid) {
		CHECK_ERR(setsid(), EPERM);
		CHECK(setpgid(0, 0) == 0);
	} else {
		CHECK(setsid() == pid);
		CHECK(getsid(0) == pid && getpgid(0) == pid);
	}

	if (failures)
		return 1;
	puts("ok");
	return 0;
}
