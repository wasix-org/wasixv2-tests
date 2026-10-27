// Another thread's execve while the main thread waits for its vfork child: Linux's de_thread kills
// the main thread in that wait (zap_other_threads; wait_for_vfork_done is killable), so it never
// returns from vfork, and the new image, the child's parent still, reaps the child. On wasixv2 the
// child runs in the main thread's instance, so the execve waits until the child is done; the main
// thread does not return from vfork either.
//#MappedDirectory: .:/t
//#ExpectedStdout: child exited=7
//#ExpectedExitCode: 0
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

// Written by the vfork child, which shares the address space.
static pid_t child;

static void *exec_soon(void *arg)
{
	struct timespec t = { 0, 5 * 1000 * 1000 };
	while (!__atomic_load_n(&child, __ATOMIC_SEQ_CST))
		nanosleep(&t, 0);
	char pid[16];
	snprintf(pid, sizeof pid, "%d", (int)child);
	char *args[] = {"/t/main", "after", pid, NULL};
	execve("/t/main", args, environ);
	perror("execve");
	_exit(1);
	return arg;
}

int main(int argc, char **argv)
{
	if (argc > 2 && strcmp(argv[1], "after") == 0) {
		pid_t pid = atoi(argv[2]);
		int status;
		if (waitpid(pid, &status, 0) != pid) {
			printf("waitpid: %s\n", strerror(errno));
			return 1;
		}
		printf("child exited=%d\n", WIFEXITED(status) ? WEXITSTATUS(status) : -1);
		return 0;
	}
	pthread_t thread;
	pthread_create(&thread, 0, exec_soon, 0);
	pid_t pid = vfork();
	if (pid == 0) {
		__atomic_store_n(&child, getpid(), __ATOMIC_SEQ_CST);
		struct timespec t = { 0, 300 * 1000 * 1000 };
		nanosleep(&t, 0);
		_exit(7);
	}
	// Not reached: the execve ended this image while it waited for the child. One write, so the
	// first call after a wrong return shows.
	static const char returned[] = "returned from vfork\n";
	write(1, returned, sizeof returned - 1);
	waitpid(pid, 0, 0);
	return 1;
}
