// An exited child stays a zombie until it is waited for: it can still be signalled (kill
// with signal 0 succeeds) and getpgid finds it; after the wait it is gone (ESRCH). A
// grandchild whose parent exits is reparented to the first process (pid 1, this test), which
// can then reap it.
//#MappedDirectory: .:/t
//#ExpectedStdout: first pid=1
//#ExpectedStdout: zombie: kill0=0 pgid=0
//#ExpectedStdout: reaped: kill0=ESRCH pgid=ESRCH
//#ExpectedStdout: orphan: middle=0 grandchild=8
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
	if (argc > 1 && strcmp(argv[1], "middle") == 0) {
		spawn("grandchild");
		return 0;
	}
	if (argc > 1 && strcmp(argv[1], "grandchild") == 0) {
		while (getppid() != 1)
			usleep(1000);
		return 8;
	}

	printf("first pid=%d\n", getpid());
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

	int status;
	pid = spawn("middle");
	waitpid(pid, &status, 0);
	int middle = WEXITSTATUS(status);
	pid_t other = waitpid(-1, &status, 0);
	printf("orphan: middle=%d grandchild=%d\n", middle,
		other > 0 && WIFEXITED(status) ? WEXITSTATUS(status) : -1);
	return 0;
}
