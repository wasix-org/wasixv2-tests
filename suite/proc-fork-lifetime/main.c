// Reclaim each child's copied address space while one parent keeps running.
// Linux kernel/fork.c:copy_process and kernel/exit.c:exit_mm,do_wait own
// memory isolation and reaping; the browser transport must not retain dead mms.
//#BuildEnv: WASIXCC_WASM_EXCEPTIONS=no
//#ExpectedStdout: forks=500 isolated=500 reaped=500
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

static volatile int value;

int main(void)
{
	int isolated = 0, reaped = 0;
	for (int i = 0; i < 500; i++) {
		value = i;
		pid_t child = fork();
		if (child < 0) { perror("fork"); return 1; }
		if (child == 0) {
			if (value != i) _exit(1);
			value = -1;
			_exit(0);
		}
		int status;
		if (waitpid(child, &status, 0) != child || !WIFEXITED(status) || WEXITSTATUS(status)) return 2;
		isolated += value == i;
		errno = 0;
		reaped += waitpid(child, &status, WNOHANG) == -1 && errno == ECHILD;
	}
	printf("forks=500 isolated=%d reaped=%d\n", isolated, reaped);
	return isolated == 500 && reaped == 500 ? 0 : 3;
}
