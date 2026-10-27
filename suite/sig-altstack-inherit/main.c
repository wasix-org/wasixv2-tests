// The alternate stack belongs to the thread: a vfork child, which runs on its parent's stack,
// keeps it, while a new thread starts without one.
//#ExpectedStdout: vfork child: same=1
//#ExpectedStdout: thread: flags=disable
//#ExpectedExitCode: 0
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

static stack_t seen;

static void *thread(void *arg)
{
	sigaltstack(0, &seen);
	return arg;
}

int main(void)
{
	size_t size = SIGSTKSZ;
	char *stack = malloc(size);
	stack_t ss = { .ss_sp = stack, .ss_size = size };
	if (sigaltstack(&ss, 0))
		return 1;
	pid_t pid = vfork();
	if (pid == 0) {
		sigaltstack(0, &seen);
		_exit(0);
	}
	waitpid(pid, 0, 0);
	printf("vfork child: same=%d\n", seen.ss_sp == stack && seen.ss_size == size && seen.ss_flags == 0);
	pthread_t t;
	pthread_create(&t, 0, thread, 0);
	pthread_join(t, 0);
	printf("thread: flags=%s\n", seen.ss_flags == SS_DISABLE ? "disable" : "other");
	return 0;
}
