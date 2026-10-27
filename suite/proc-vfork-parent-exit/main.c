// A vfork parent whose thread group exits while its child runs stays until the child is done:
// the child runs in the parent's instance, so the parent cannot be reaped before then. Its
// status is the group's.
//#MappedDirectory: .:/t
//#ExpectedStdout: child done
//#ExpectedStdout: parent exited=3
//#ExpectedExitCode: 0
#include <pthread.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

static void *exit_group_soon(void *arg)
{
	struct timespec t = { 0, 50 * 1000 * 1000 };
	nanosleep(&t, 0);
	_exit(3);
	return arg;
}

int main(int argc, char **argv)
{
	if (argc > 1 && strcmp(argv[1], "parent") == 0) {
		pthread_t thread;
		pthread_create(&thread, 0, exit_group_soon, 0);
		pid_t pid = vfork();
		if (pid == 0) {
			struct timespec t = { 0, 300 * 1000 * 1000 };
			nanosleep(&t, 0);
			write(1, "child done\n", 11);
			_exit(0);
		}
		waitpid(pid, 0, 0);
		return 1;
	}
	char *args[] = {"/t/main", "parent", NULL};
	pid_t pid;
	if (posix_spawn(&pid, "/t/main", NULL, NULL, args, environ))
		return 1;
	int status;
	waitpid(pid, &status, 0);
	printf("parent exited=%d\n", WIFEXITED(status) ? WEXITSTATUS(status) : -1);
	return 0;
}
