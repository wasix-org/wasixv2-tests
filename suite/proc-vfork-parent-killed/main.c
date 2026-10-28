// SIGKILL to a parent waiting for its vfork child kills it at once (`wait_for_vfork_done` is
// killable): its parent reaps it long before the child ends, while the child runs on and goes
// to init, which reaps it.
//#MappedDirectory: .:/t
//#ExpectedStdout: parent: killed=1 sig=9 before-child=1
//#ExpectedStdout: child: alive=1 ppid=1 reaped=1
#include <errno.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

static long now_ms(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static void sleep_ms(long ms)
{
	struct timespec t = { ms / 1000, (ms % 1000) * 1000 * 1000 };
	nanosleep(&t, 0);
}

// The PPid line of /proc/<pid>/status, or -1.
static int ppid_of(pid_t pid)
{
	char path[64], line[128];
	snprintf(path, sizeof path, "/proc/%d/status", pid);
	FILE *f = fopen(path, "r");
	int ppid = -1;
	while (f && fgets(line, sizeof line, f))
		if (sscanf(line, "PPid: %d", &ppid) == 1)
			break;
	if (f)
		fclose(f);
	return ppid;
}

int main(int argc, char **argv)
{
	if (argc > 2 && strcmp(argv[1], "parent") == 0) {
		int fd = atoi(argv[2]);
		pid_t pid = vfork();
		if (pid == 0) {
			dprintf(fd, "%d\n", getpid());
			sleep_ms(2000);
			_exit(7);
		}
		waitpid(pid, 0, 0);
		return 1;
	}

	int fds[2];
	if (pipe(fds))
		return 1;
	char fd[16];
	snprintf(fd, sizeof fd, "%d", fds[1]);
	char *args[] = {"/t/main", "parent", fd, NULL};
	pid_t parent;
	if (posix_spawn(&parent, "/t/main", NULL, NULL, args, environ))
		return 1;
	close(fds[1]);
	char buf[32] = {0};
	if (read(fds[0], buf, sizeof buf - 1) <= 0)
		return 2;
	pid_t child = atoi(buf);
	long start = now_ms();
	sleep_ms(200);
	kill(parent, SIGKILL);
	int status;
	if (waitpid(parent, &status, 0) != parent)
		return 3;
	long waited = now_ms() - start;
	printf("parent: killed=%d sig=%d before-child=%d\n", WIFSIGNALED(status),
	       WIFSIGNALED(status) ? WTERMSIG(status) : -1, waited < 1500);
	int alive = kill(child, 0) == 0;
	int ppid = ppid_of(child);
	int reaped = 0;
	for (int i = 0; i < 1000 && !reaped; i++) {
		reaped = kill(child, 0) < 0 && errno == ESRCH;
		if (!reaped)
			sleep_ms(10);
	}
	printf("child: alive=%d ppid=%d reaped=%d\n", alive, ppid, reaped);
	return 0;
}
