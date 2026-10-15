// execve replaces the image and keeps the pid: the new image gets the argv and envp it was
// given, not the old ones, and its exit status is the process's.
//#MappedDirectory: .:/t
//#Env: OLD=1
//#ExpectedStdout: before exec
//#ExpectedStdout: child argc=3 argv[1]=child same-pid=1 NEW=yes OLD=(null)
//#ExpectedExitCode: 7
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	if (argc > 1 && strcmp(argv[1], "child") == 0) {
		int same = atoi(argv[2]) == getpid();
		const char *old = getenv("OLD");
		printf("child argc=%d argv[1]=%s same-pid=%d NEW=%s OLD=%s\n", argc, argv[1], same,
			getenv("NEW"), old ? old : "(null)");
		return 7;
	}
	char pid[16];
	snprintf(pid, sizeof pid, "%d", getpid());
	char *args[] = {"/t/main", "child", pid, NULL};
	char *env[] = {"NEW=yes", NULL};
	printf("before exec\n");
	fflush(stdout);
	execve("/t/main", args, env);
	perror("execve");
	return 1;
}
