// The wait status encodes how a child ended: exit codes keep their low byte, and a child a
// signal killed is WIFSIGNALED with that signal (no core dump: RLIMIT_CORE is 0). WNOHANG on
// a running child returns 0, and waiting without children fails with ECHILD.
//#MappedDirectory: .:/t
//#ExpectedStdout: exit 0: exited=1 code=0
//#ExpectedStdout: exit 255: exited=1 code=255
//#ExpectedStdout: exit 259: exited=1 code=3
//#ExpectedStdout: abort: signaled=1 sig=6 core=0
//#ExpectedStdout: wnohang=0
//#ExpectedStdout: kill TERM: signaled=1 sig=15
//#ExpectedStdout: kill KILL: signaled=1 sig=9
//#ExpectedStdout: no children=ECHILD
#include <errno.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

static pid_t spawn(char *mode, char *arg)
{
	char *args[] = {"/t/main", mode, arg, NULL};
	pid_t pid;
	int err = posix_spawn(&pid, "/t/main", NULL, NULL, args, environ);
	if (err) {
		printf("posix_spawn: %s\n", strerror(err));
		exit(1);
	}
	return pid;
}

static int reap(pid_t pid)
{
	int status;
	if (waitpid(pid, &status, 0) != pid) {
		perror("waitpid");
		exit(1);
	}
	return status;
}

static void exited(const char *what, int status)
{
	printf("%s: exited=%d code=%d\n", what, WIFEXITED(status), WEXITSTATUS(status));
}

int main(int argc, char **argv)
{
	if (argc > 2 && strcmp(argv[1], "exit") == 0)
		exit(atoi(argv[2]));
	if (argc > 1 && strcmp(argv[1], "abort") == 0)
		abort();
	if (argc > 1 && strcmp(argv[1], "pause") == 0) {
		for (;;)
			pause();
	}

	exited("exit 0", reap(spawn("exit", "0")));
	exited("exit 255", reap(spawn("exit", "255")));
	exited("exit 259", reap(spawn("exit", "259")));
	int status = reap(spawn("abort", NULL));
	printf("abort: signaled=%d sig=%d core=%d\n", WIFSIGNALED(status), WTERMSIG(status),
		WCOREDUMP(status) != 0);

	pid_t pid = spawn("pause", NULL);
	printf("wnohang=%d\n", waitpid(pid, &status, WNOHANG));
	kill(pid, SIGTERM);
	status = reap(pid);
	printf("kill TERM: signaled=%d sig=%d\n", WIFSIGNALED(status), WTERMSIG(status));
	pid = spawn("pause", NULL);
	kill(pid, SIGKILL);
	status = reap(pid);
	printf("kill KILL: signaled=%d sig=%d\n", WIFSIGNALED(status), WTERMSIG(status));

	errno = 0;
	waitpid(-1, &status, 0);
	printf("no children=%s\n", errno == ECHILD ? "ECHILD" : strerror(errno));
	return 0;
}
