// A thread's exit_group while the main thread waits for its vfork child ends the process at
// once: its parent reaps it with the thread's status long before the child ends, while the
// child runs on and goes to init.
//#MappedDirectory: .:/t
//#ExpectedStdout: parent: exited=3 before-child=1
//#ExpectedStdout: child: alive=1 ppid=1
#include <pthread.h>
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

static void *exit_group_soon(void *arg)
{
	sleep_ms(200);
	_exit(3);
	return arg;
}

int main(int argc, char **argv)
{
	if (argc > 2 && strcmp(argv[1], "parent") == 0) {
		int fd = atoi(argv[2]);
		pthread_t thread;
		pthread_create(&thread, 0, exit_group_soon, 0);
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
	int status;
	if (waitpid(parent, &status, 0) != parent)
		return 3;
	long waited = now_ms() - start;
	printf("parent: exited=%d before-child=%d\n", WIFEXITED(status) ? WEXITSTATUS(status) : -1,
	       waited < 1500);
	printf("child: alive=%d ppid=%d\n", kill(child, 0) == 0, ppid_of(child));
	return 0;
}
