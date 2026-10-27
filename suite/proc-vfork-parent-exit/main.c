// A vfork parent whose thread group exits while its child runs ends with the group's status, and
// the child, which runs on in the parent's address space, goes to the subreaper and ends as it
// chooses. On Linux the parent dies at once (`wait_for_vfork_done` is killable); on wasixv2 the
// child runs in the parent's instance, so the parent becomes a zombie once the child is done.
// Either way the subreaper reaps the parent, then the child.
//#MappedDirectory: .:/t
//#ExpectedStdout: parent exited=3
//#ExpectedStdout: child exited=7
//#ExpectedExitCode: 0
#include <pthread.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
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

static int status_of(int status)
{
	return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
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
			_exit(7);
		}
		waitpid(pid, 0, 0);
		return 1;
	}
	// The vfork child goes to us when the parent exits.
	prctl(PR_SET_CHILD_SUBREAPER, 1);
	char *args[] = {"/t/main", "parent", NULL};
	pid_t pid;
	if (posix_spawn(&pid, "/t/main", NULL, NULL, args, environ))
		return 1;
	int status;
	if (waitpid(pid, &status, 0) != pid)
		return 1;
	printf("parent exited=%d\n", status_of(status));
	if (waitpid(-1, &status, 0) < 0)
		return 1;
	printf("child exited=%d\n", status_of(status));
	return 0;
}
