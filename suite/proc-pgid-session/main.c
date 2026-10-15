// Process groups and sessions: the first process leads its session and group, so setsid and
// setpgid on itself fail with EPERM. A child starts in its parent's group and session, and can
// start its own session, whose leader cannot change its group (EPERM). setpgid rejects a
// negative group (EINVAL), a group that does not exist in the session (EPERM), a process that
// is neither the caller nor its child (ESRCH), and a child that has exec'd (EACCES).
//#MappedDirectory: .:/t
//#ExpectedStdout: leader: pgid=1 sid=1 setsid=EPERM setpgid=EPERM
//#ExpectedStdout: child: inherits=1 einval=EINVAL eperm=EPERM setsid=1 leader=1 again=EPERM
//#ExpectedStdout: parent: child pgid=own esrch=ESRCH eacces=EACCES
#include <errno.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

static const char *res(int r)
{
	if (r >= 0)
		return "ok";
	switch (errno) {
	case EPERM: return "EPERM";
	case EINVAL: return "EINVAL";
	case ESRCH: return "ESRCH";
	case EACCES: return "EACCES";
	default: return strerror(errno);
	}
}

int main(int argc, char **argv)
{
	if (argc > 1 && strcmp(argv[1], "pause") == 0) {
		for (;;)
			pause();
	}

	pid_t me = getpid();
	printf("leader: pgid=%d sid=%d setsid=%s ", getpgid(0) == me, getsid(0) == me,
		res(setsid()));
	printf("setpgid=%s\n", res(setpgid(0, 0)));
	fflush(stdout);

	pid_t pid = vfork();
	if (pid == 0) {
		pid_t self = getpid();
		int inherits = getpgid(0) == getpgid(me) && getsid(0) == getsid(me);
		printf("child: inherits=%d ", inherits);
		printf("einval=%s ", res(setpgid(0, -1)));
		printf("eperm=%s ", res(setpgid(0, 99999)));
		int sid = setsid();
		printf("setsid=%d leader=%d ", sid == self, getsid(0) == self && getpgid(0) == self);
		printf("again=%s\n", res(setpgid(0, 0)));
		fflush(stdout);
		_exit(0);
	}
	printf("parent: child pgid=%s ", getpgid(pid) == pid ? "own" : "other");
	waitpid(pid, NULL, 0);
	printf("esrch=%s ", res(setpgid(99999, 0)));
	char *args[] = {"/t/main", "pause", NULL};
	if (posix_spawn(&pid, "/t/main", NULL, NULL, args, environ)) {
		printf("posix_spawn failed\n");
		return 1;
	}
	printf("eacces=%s\n", res(setpgid(pid, pid)));
	kill(pid, SIGKILL);
	waitpid(pid, NULL, 0);
	return 0;
}
