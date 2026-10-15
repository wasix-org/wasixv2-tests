// vfork: the child runs in the parent's memory with its own pid and the parent as its parent,
// the parent waits until the child execs or exits, and vfork then returns the child's pid.
// An exec'd child keeps running on its own; a child that calls _exit is reaped with its
// status, and one a signal kills resumes the parent too.
//#MappedDirectory: .:/t
//#ExpectedStdout: exec child ppid-ok=1
//#ExpectedStdout: exec: status=9
//#ExpectedStdout: exit: shared=42 status=5
//#ExpectedStdout: child pid differs=1
//#ExpectedStdout: killed: signaled=1 sig=9
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static volatile int shared;

int main(int argc, char **argv)
{
	if (argc > 1 && strcmp(argv[1], "child") == 0) {
		printf("exec child ppid-ok=%d\n", getppid() == atoi(argv[2]));
		return 9;
	}
	char ppid[16];
	snprintf(ppid, sizeof ppid, "%d", getpid());
	fflush(stdout);
	pid_t pid = vfork();
	if (pid == 0) {
		char *args[] = {"/t/main", "child", ppid, NULL};
		execv("/t/main", args);
		_exit(127);
	}
	int status = 0;
	if (pid < 0 || waitpid(pid, &status, 0) != pid) {
		perror("vfork/waitpid");
		return 1;
	}
	printf("exec: status=%d\n", WIFEXITED(status) ? WEXITSTATUS(status) : -1);

	pid_t me = getpid();
	volatile pid_t seen = 0;
	pid = vfork();
	if (pid == 0) {
		shared = 42;
		seen = getpid();
		_exit(5);
	}
	if (pid < 0 || waitpid(pid, &status, 0) != pid) {
		perror("vfork/waitpid");
		return 1;
	}
	printf("exit: shared=%d status=%d\n", shared, WIFEXITED(status) ? WEXITSTATUS(status) : -1);
	printf("child pid differs=%d\n", seen == pid && seen != me);

	pid = vfork();
	if (pid == 0) {
		kill(getpid(), SIGKILL);
		_exit(1);
	}
	if (pid < 0 || waitpid(pid, &status, 0) != pid) {
		perror("vfork/waitpid");
		return 1;
	}
	printf("killed: signaled=%d sig=%d\n", WIFSIGNALED(status), WTERMSIG(status));
	return 0;
}
