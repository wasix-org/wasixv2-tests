// posix_spawn and posix_spawnp: file actions redirect the child's stdout into a pipe, the
// child's exit status comes back through waitpid, POSIX_SPAWN_SETPGROUP puts the child in its
// own process group, and a missing program is reported as the return value (ENOENT).
//#MappedDirectory: .:/t
//#ExpectedStdout: piped: hello from 3
//#ExpectedStdout: status=3
//#ExpectedStdout: hello from 4
//#ExpectedStdout: spawnp status=4
//#ExpectedStdout: own group=1
//#ExpectedStdout: missing=ENOENT
#include <errno.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

static int wait_status(pid_t pid)
{
	int status;
	if (waitpid(pid, &status, 0) != pid) {
		perror("waitpid");
		exit(1);
	}
	return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

int main(int argc, char **argv)
{
	if (argc > 2 && strcmp(argv[1], "say") == 0) {
		printf("hello from %s\n", argv[2]);
		return atoi(argv[2]);
	}
	if (argc > 1 && strcmp(argv[1], "group") == 0)
		return getpgid(0) == getpid() ? 1 : 0;

	int p[2];
	if (pipe(p)) {
		perror("pipe");
		return 1;
	}
	posix_spawn_file_actions_t fa;
	posix_spawn_file_actions_init(&fa);
	posix_spawn_file_actions_adddup2(&fa, p[1], 1);
	posix_spawn_file_actions_addclose(&fa, p[0]);
	pid_t pid;
	char *say[] = {"/t/main", "say", "3", NULL};
	int err = posix_spawn(&pid, "/t/main", &fa, NULL, say, environ);
	if (err) {
		printf("posix_spawn: %s\n", strerror(err));
		return 1;
	}
	close(p[1]);
	char buf[64] = "";
	int n = 0, r;
	while ((r = read(p[0], buf + n, sizeof buf - 1 - n)) > 0)
		n += r;
	buf[n] = 0;
	printf("piped: %s", buf);
	printf("status=%d\n", wait_status(pid));

	setenv("PATH", "/nowhere:/t", 1);
	char *say4[] = {"main", "say", "4", NULL};
	fflush(stdout);
	err = posix_spawnp(&pid, "main", NULL, NULL, say4, environ);
	if (err) {
		printf("posix_spawnp: %s\n", strerror(err));
		return 1;
	}
	int status = wait_status(pid);
	printf("spawnp status=%d\n", status);

	posix_spawnattr_t attr;
	posix_spawnattr_init(&attr);
	posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
	posix_spawnattr_setpgroup(&attr, 0);
	char *group[] = {"/t/main", "group", NULL};
	err = posix_spawn(&pid, "/t/main", NULL, &attr, group, environ);
	if (err) {
		printf("posix_spawn: %s\n", strerror(err));
		return 1;
	}
	printf("own group=%d\n", wait_status(pid));

	err = posix_spawn(&pid, "/t/missing", NULL, NULL, group, environ);
	printf("missing=%s\n", err == ENOENT ? "ENOENT" : strerror(err));
	return 0;
}
