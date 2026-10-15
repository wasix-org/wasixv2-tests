// PR_SET_CHILD_SUBREAPER: orphans go to the nearest subreaper ancestor instead of pid 1, and
// the subreaper can wait for them. The flag is not inherited.
//#MappedDirectory: .:/t
//#ExpectedStdout: reaper: inherited=0 orphan=8 ppid-was-reaper=1
//#ExpectedStdout: reaper exited=0
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

static pid_t spawn(char *mode, char *arg)
{
	char *args[] = {"/t/main", mode, arg, NULL};
	pid_t pid;
	int err = posix_spawn(&pid, "/t/main", NULL, NULL, args, environ);
	if (err) {
		printf("posix_spawn: %s\n", strerror(err));
		exit(1);
	}
	return pid;
}

int main(int argc, char **argv)
{
	if (argc > 1 && strcmp(argv[1], "reaper") == 0) {
		int inherited = -1;
		prctl(PR_GET_CHILD_SUBREAPER, &inherited);
		prctl(PR_SET_CHILD_SUBREAPER, 1);
		char me[16];
		snprintf(me, sizeof me, "%d", getpid());
		pid_t middle = spawn("middle", me);
		waitpid(middle, NULL, 0);
		int status;
		pid_t orphan = waitpid(-1, &status, 0);
		printf("reaper: inherited=%d orphan=%d ppid-was-reaper=%d\n", inherited,
			orphan > 0 ? WEXITSTATUS(status) / 10 : -1,
			orphan > 0 ? WEXITSTATUS(status) % 10 : -1);
		return 0;
	}
	if (argc > 1 && strcmp(argv[1], "middle") == 0) {
		spawn("orphan", argv[2]);
		return 0;
	}
	if (argc > 1 && strcmp(argv[1], "orphan") == 0) {
		pid_t reaper = atoi(argv[2]);
		while (getppid() != reaper && getppid() != 1)
			usleep(1000);
		return 80 + (getppid() == reaper);
	}

	prctl(PR_SET_CHILD_SUBREAPER, 1);
	fflush(stdout);
	pid_t reaper = spawn("reaper", NULL);
	int status;
	waitpid(reaper, &status, 0);
	printf("reaper exited=%d\n", WEXITSTATUS(status));
	return 0;
}
