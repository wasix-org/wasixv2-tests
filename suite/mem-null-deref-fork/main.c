// A forked child gets its parent's memory copied and the same 64 KiB guard at address 0: a null
// pointer dereference in the child kills it with SIGSEGV, and the parent, whose memory was the
// source of the copy, keeps its own guard.
//#BuildEnv: WASIXCC_WASM_EXCEPTIONS=no
//#ExpectedStdout: child: signaled=1 sig=11
//#ExpectedStdout: parent: signaled=1 sig=11
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

static volatile uintptr_t null_addr;

static pid_t fork_and_wait(int *status)
{
	pid_t pid = fork();
	if (pid < 0) {
		perror("fork");
		exit(1);
	}
	if (pid > 0 && waitpid(pid, status, 0) != pid) {
		perror("waitpid");
		exit(1);
	}
	return pid;
}

int main(void)
{
	int status;
	if (fork_and_wait(&status) == 0) {
		*(volatile int *)null_addr = 1;
		_exit(0);
	}
	printf("child: signaled=%d sig=%d\n", WIFSIGNALED(status), WTERMSIG(status));

	/* The source of a fork copy keeps its guard: a child forks a grandchild of its own, which
	 * just exits, and then faults itself. */
	if (fork_and_wait(&status) == 0) {
		if (fork_and_wait(&status) == 0)
			_exit(0);
		*(volatile int *)null_addr = 1;
		_exit(0);
	}
	printf("parent: signaled=%d sig=%d\n", WIFSIGNALED(status), WTERMSIG(status));
	return 0;
}
