// The leader's execve while another thread waits in vfork: Linux's de_thread kills that thread
// in wait_for_vfork_done (zap_other_threads; the wait is killable) instead of waiting for its
// vfork child, so the new image starts at once, while the child runs on and ends as a child of
// the process. The thread never returns from vfork.
//#MappedDirectory: .:/t
//#ExpectedStdout: same pid=1
//#ExpectedStdout: started before the child ended=1
//#ExpectedStdout: child exited=5
//#ExpectedExitCode: 0
#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define CHILD_MS 1500

static long long now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
}

static void sleep_ms(int ms)
{
	struct timespec t = {ms / 1000, (ms % 1000) * 1000000L};
	nanosleep(&t, 0);
}

static void *vfork_and_wait(void *arg)
{
	(void)arg;
	pid_t pid = vfork();
	if (pid == 0) {
		sleep_ms(CHILD_MS);
		_exit(5);
	}
	/* The execve kills the thread in vfork; it never gets here. */
	puts("FAIL the thread returned from vfork");
	fflush(stdout);
	return NULL;
}

/* The new image: the same process, started while the vfork child still runs. */
static int after(char **argv)
{
	long long started = now_ms() - atoll(argv[3]);
	printf("same pid=%d\n", getpid() == atoi(argv[2]));
	printf("started before the child ended=%d\n", started < CHILD_MS - 500);
	fflush(stdout);
	int status;
	if (waitpid(-1, &status, 0) < 0) {
		perror("waitpid");
		return 1;
	}
	printf("child exited=%d\n", WIFEXITED(status) ? WEXITSTATUS(status) : -1);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc > 3 && strcmp(argv[1], "after") == 0)
		return after(argv);

	pthread_t thread;
	if (pthread_create(&thread, 0, vfork_and_wait, 0))
		return 1;
	sleep_ms(100);
	char pid[16], start[32];
	snprintf(pid, sizeof pid, "%d", getpid());
	snprintf(start, sizeof start, "%lld", now_ms());
	char *args[] = {"/t/main", "after", pid, start, NULL};
	execv("/t/main", args);
	perror("execv");
	return 2;
}
