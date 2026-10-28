// fork needs the asyncify build configuration. Built in the default one (wasm exceptions), it
// fails with ENOSYS, as it does on Linux without an MMU, and creates no child, and so does clone()
// without CLONE_VM; vfork still works.
//#ExpectedStdout: fork=-1 ENOSYS=1 no-child=1
//#ExpectedStdout: clone=-1 ENOSYS=1 no-child=1
//#ExpectedStdout: vfork: status=6
#define _GNU_SOURCE
#include <errno.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

static char stack[4096] __attribute__((aligned(16)));

static int child(void *arg)
{
	(void)arg;
	return 0;
}

int main(void)
{
	pid_t pid = fork();
	int err = errno;
	if (pid == 0) _exit(0);
	int none = waitpid(-1, NULL, WNOHANG) < 0 && errno == ECHILD;
	printf("fork=%d ENOSYS=%d no-child=%d\n", pid, err == ENOSYS, none);

	pid = clone(child, stack + sizeof stack, SIGCHLD, NULL);
	err = errno;
	none = waitpid(-1, NULL, WNOHANG) < 0 && errno == ECHILD;
	printf("clone=%d ENOSYS=%d no-child=%d\n", pid, err == ENOSYS, none);

	fflush(stdout);
	pid = vfork();
	if (pid == 0) _exit(6);
	int status;
	if (pid < 0 || waitpid(pid, &status, 0) != pid) {
		perror("vfork");
		return 1;
	}
	printf("vfork: status=%d\n", WEXITSTATUS(status));
	return 0;
}
