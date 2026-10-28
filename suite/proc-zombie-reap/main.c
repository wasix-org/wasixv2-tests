// An exited child stays a zombie until it is waited for: it can still be signalled (kill
// with signal 0 succeeds) and getpgid finds it; after the wait it is gone (ESRCH). Orphans
// go to init (proc-init-pid1) or a subreaper (proc-subreaper).
//#MappedDirectory: .:/t
//#ExpectedStdout: zombie: kill0=0 pgid=0
//#ExpectedStdout: reaped: kill0=ESRCH pgid=ESRCH
#include <errno.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

static pid_t spawn(char *mode)
{
	char *args[] = {"/t/main", mode, NULL};
	pid_t pid;
	int err = posix_spawn(&pid, "/t/main", NULL, NULL, args, environ);
	if (err) {
		printf("posix_spawn: %s\n", strerror(err));
		exit(1);
	}
	return pid;
}

static const char *res(int r)
{
	return r == 0 ? "0" : errno == ESRCH ? "ESRCH" : strerror(errno);
}

int main(int argc, char **argv)
{
	if (argc > 1 && strcmp(argv[1], "exit") == 0)
		return 0;
	pid_t pid = spawn("exit");
	siginfo_t si;
	// Wait for the exit without reaping.
	waitid(P_PID, pid, &si, WEXITED | WNOWAIT);
	int k = kill(pid, 0);
	const char *kr = res(k);
	int g = getpgid(pid) == getpgid(0) ? 0 : -1;
	printf("zombie: kill0=%s pgid=%s\n", kr, res(g));
	waitpid(pid, NULL, 0);
	errno = 0;
	k = kill(pid, 0);
	kr = res(k);
	errno = 0;
	g = getpgid(pid);
	printf("reaped: kill0=%s pgid=%s\n", kr, res(g < 0 ? -1 : 0));
	return 0;
}
