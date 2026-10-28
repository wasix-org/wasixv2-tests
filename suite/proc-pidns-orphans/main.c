// Orphans in a pid namespace go to its init: after unshare(CLONE_NEWPID), the namespace's init
// (pid 1) forks a child (pid 2), which forks a grandchild (pid 3) and exits; the grandchild's
// parent is then the init, which waits for both and hears of the grandchild by its pid there.
// The init's own parent is outside the namespace (getppid is 0), and the caller hears of the
// init by the pid fork returned. Without CAP_SYS_ADMIN (a uid other than 0), unshare fails
// with EPERM.
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: CHECK(%s) failed (errno %s)\n", __LINE__, #c, strerror(errno)); fails++; } } while (0)
#define FAILS(call, e) do { errno = 0; long r_ = (long)(call); if (r_ != -1 || errno != (e)) { fprintf(stderr, "line %d: %s = %ld (%s), want -1 %s\n", __LINE__, #call, r_, strerror(errno), #e); fails++; } } while (0)

/// The grandchild: waits until its parent is gone, then reports whose child it is now.
static void orphan(int parent_gone)
{
	char c;
	while (read(parent_gone, &c, 1) < 0 && errno == EINTR) {
	}
	_exit(getppid() == 1 ? 33 : 34);
}

static int init(void)
{
	CHECK(getpid() == 1);
	CHECK(getppid() == 0);
	int gone[2];
	CHECK(pipe(gone) == 0);
	pid_t child = fork();
	if (child == 0) {
		pid_t grandchild = fork();
		if (grandchild == 0) {
			close(gone[1]);
			orphan(gone[0]);
		}
		_exit(grandchild == 3 ? 0 : 1);
	}
	close(gone[0]);
	CHECK(child == 2);
	int status;
	CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
	// The child is reaped, so the grandchild's parent is gone: let it look.
	close(gone[1]);
	siginfo_t info;
	memset(&info, 0, sizeof info);
	CHECK(waitid(P_ALL, 0, &info, WEXITED) == 0);
	CHECK(info.si_pid == 3);
	CHECK(info.si_code == CLD_EXITED && info.si_status == 33);
	CHECK(waitpid(-1, NULL, WNOHANG) == -1 && errno == ECHILD);
	return fails;
}

int main(void)
{
	if (geteuid() != 0) {
		FAILS(unshare(CLONE_NEWPID), EPERM);
	} else {
		CHECK(unshare(CLONE_NEWPID) == 0);
		pid_t pid = fork();
		if (pid == 0)
			_exit(init() ? 1 : 0);
		CHECK(pid > 1);
		siginfo_t info;
		memset(&info, 0, sizeof info);
		CHECK(waitid(P_PID, pid, &info, WEXITED) == 0);
		CHECK(info.si_pid == pid);
		CHECK(info.si_code == CLD_EXITED && info.si_status == 0);
	}
	if (fails)
		return 1;
	puts("PASS");
	return 0;
}
