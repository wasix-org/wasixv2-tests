// kill(-pgid) signals every process in the group and nobody else; kill(0) signals the
// caller's own group, the caller included. A group with no members is ESRCH.
//#MappedDirectory: .:/t
//#ExpectedStdout: group: a=15 b=15 outsider-alive=1
//#ExpectedStdout: own group: handled=1 child=10
//#ExpectedStdout: empty group=ESRCH
#include <errno.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

static pid_t spawn_in(pid_t pgroup, int set)
{
	char *args[] = {"/t/main", "pause", NULL};
	posix_spawnattr_t attr;
	posix_spawnattr_init(&attr);
	if (set) {
		posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
		posix_spawnattr_setpgroup(&attr, pgroup);
	}
	pid_t pid;
	int err = posix_spawn(&pid, "/t/main", NULL, &attr, args, environ);
	if (err) {
		printf("posix_spawn: %s\n", strerror(err));
		exit(1);
	}
	return pid;
}

static int termsig(pid_t pid)
{
	int status;
	if (waitpid(pid, &status, 0) != pid)
		return -1;
	return WIFSIGNALED(status) ? WTERMSIG(status) : -1;
}

static volatile sig_atomic_t handled;

static void on_usr1(int sig)
{
	(void)sig;
	handled = 1;
}

int main(int argc, char **argv)
{
	if (argc > 1 && strcmp(argv[1], "pause") == 0) {
		for (;;)
			pause();
	}

	pid_t a = spawn_in(0, 1);
	pid_t b = spawn_in(a, 1);
	pid_t outsider = spawn_in(0, 0);
	kill(-a, SIGTERM);
	int sa = termsig(a), sb = termsig(b);
	int status;
	int alive = waitpid(outsider, &status, WNOHANG) == 0;
	printf("group: a=%d b=%d outsider-alive=%d\n", sa, sb, alive);

	signal(SIGUSR1, on_usr1);
	kill(0, SIGUSR1);
	printf("own group: handled=%d child=%d\n", handled, termsig(outsider));

	errno = 0;
	kill(-a, SIGTERM);
	printf("empty group=%s\n", errno == ESRCH ? "ESRCH" : strerror(errno));
	return 0;
}
