// A thread's execve while the leader waits in vfork, as in proc-vfork-sibling-exec, then the
// vfork child traps. Only the child dies (of SIGILL, from __builtin_trap): the new image, which
// took over the process, runs on and reaps it.
//#MappedDirectory: .:/t
//#ExpectedStdout: same pid=1
//#ExpectedStdout: child signaled=4
//#ExpectedExitCode: 0
#define _GNU_SOURCE
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static void sleep_ms(int ms)
{
	struct timespec t = {ms / 1000, (ms % 1000) * 1000000L};
	nanosleep(&t, 0);
}

static void *exec_soon(void *arg)
{
	(void)arg;
	sleep_ms(100);
	char pid[16];
	snprintf(pid, sizeof pid, "%d", getpid());
	char *args[] = {"/t/main", "after", pid, NULL};
	execv("/t/main", args);
	perror("execv");
	_exit(2);
}

/* The new image: the same process, which reaps the child once it trapped. */
static int after(char **argv)
{
	printf("same pid=%d\n", getpid() == atoi(argv[2]));
	fflush(stdout);
	int status;
	if (waitpid(-1, &status, 0) < 0) {
		perror("waitpid");
		return 1;
	}
	printf("child signaled=%d\n", WIFSIGNALED(status) ? WTERMSIG(status) : -1);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc > 2 && strcmp(argv[1], "after") == 0)
		return after(argv);

	pthread_t thread;
	if (pthread_create(&thread, 0, exec_soon, 0))
		return 1;
	pid_t pid = vfork();
	if (pid == 0) {
		sleep_ms(600);
		__builtin_trap();
	}
	/* The execve kills the leader in vfork; it never gets here. */
	puts("FAIL the leader returned from vfork");
	return 1;
}
