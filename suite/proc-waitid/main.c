// waitid: the siginfo it fills (SIGCHLD, CLD_EXITED or CLD_KILLED, si_pid, si_status),
// WNOWAIT leaving the zombie for a second wait, WNOHANG with nothing ready zeroing si_pid,
// P_PGID and P_PIDFD selecting children, and the EINVAL and ECHILD cases.
//#MappedDirectory: .:/t
//#ExpectedStdout: exited: signo=1 code=CLD_EXITED pid=1 status=4
//#ExpectedStdout: wnowait: again=1 then=ECHILD
//#ExpectedStdout: wnohang: ret=0 pid=0
//#ExpectedStdout: killed: code=CLD_KILLED status=9
//#ExpectedStdout: pgid: found=1
//#ExpectedStdout: pidfd: found=1
//#ExpectedStdout: no options=EINVAL bad idtype=EINVAL pid 0=EINVAL other=ECHILD
#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

static pid_t spawn(char *mode, char *arg, int own_group)
{
	char *args[] = {"/t/main", mode, arg, NULL};
	posix_spawnattr_t attr;
	posix_spawnattr_init(&attr);
	if (own_group) {
		posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
		posix_spawnattr_setpgroup(&attr, 0);
	}
	pid_t pid;
	int err = posix_spawn(&pid, "/t/main", NULL, &attr, args, environ);
	if (err) {
		printf("posix_spawn: %s\n", strerror(err));
		exit(1);
	}
	return pid;
}

static const char *err(int r)
{
	if (r == 0)
		return "0";
	switch (errno) {
	case EINVAL: return "EINVAL";
	case ECHILD: return "ECHILD";
	default: return strerror(errno);
	}
}

int main(int argc, char **argv)
{
	if (argc > 2 && strcmp(argv[1], "exit") == 0)
		return atoi(argv[2]);
	if (argc > 1 && strcmp(argv[1], "pause") == 0) {
		for (;;)
			pause();
	}

	siginfo_t si;
	pid_t pid = spawn("exit", "4", 0);
	memset(&si, 0xff, sizeof si);
	if (waitid(P_PID, pid, &si, WEXITED)) {
		perror("waitid");
		return 1;
	}
	printf("exited: signo=%d code=%s pid=%d status=%d\n", si.si_signo == SIGCHLD,
		si.si_code == CLD_EXITED ? "CLD_EXITED" : "other", si.si_pid == pid, si.si_status);

	pid = spawn("exit", "0", 0);
	waitid(P_PID, pid, &si, WEXITED | WNOWAIT);
	memset(&si, 0, sizeof si);
	int again = waitid(P_PID, pid, &si, WEXITED) == 0 && si.si_pid == pid;
	printf("wnowait: again=%d then=%s\n", again, err(waitid(P_PID, pid, &si, WEXITED)));

	pid = spawn("pause", NULL, 0);
	memset(&si, 0xff, sizeof si);
	int ret = waitid(P_ALL, 0, &si, WEXITED | WNOHANG);
	printf("wnohang: ret=%d pid=%d\n", ret, si.si_pid);
	kill(pid, SIGKILL);
	waitid(P_PID, pid, &si, WEXITED);
	printf("killed: code=%s status=%d\n", si.si_code == CLD_KILLED ? "CLD_KILLED" : "other",
		si.si_status);

	pid = spawn("exit", "0", 1);
	memset(&si, 0, sizeof si);
	waitid(P_PGID, pid, &si, WEXITED);
	printf("pgid: found=%d\n", si.si_pid == pid);

	pid = spawn("exit", "0", 0);
	int fd = syscall(SYS_pidfd_open, pid, 0);
	memset(&si, 0, sizeof si);
	if (fd < 0 || waitid(P_PIDFD, fd, &si, WEXITED)) {
		perror("pidfd");
		return 1;
	}
	printf("pidfd: found=%d\n", si.si_pid == pid);

	printf("no options=%s ", err(waitid(P_ALL, 0, &si, 0)));
	printf("bad idtype=%s ", err(waitid(7, 0, &si, WEXITED)));
	printf("pid 0=%s ", err(waitid(P_PID, 0, &si, WEXITED)));
	printf("other=%s\n", err(waitid(P_PID, 1, &si, WEXITED)));
	return 0;
}
