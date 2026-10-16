// A process whose main thread returns while other threads spin, sleep, wait on a futex or block
// in read ends with the status of main, every time: the other threads die at their next
// preemption point or wake from their wait, and nothing of their teardown shows. The parent
// sees the exit only once they are gone.
//#MappedDirectory: .:/t
//#ExpectedStdout: 200 runs: all exited 0=1
#include <pthread.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t never = PTHREAD_COND_INITIALIZER;

static void *spin(void *arg)
{
	for (volatile unsigned long i = 0;; i++) {
	}
	return arg;
}

static void *doze(void *arg)
{
	sleep(100);
	return arg;
}

static void *wait_futex(void *arg)
{
	pthread_mutex_lock(&lock);
	for (;;)
		pthread_cond_wait(&never, &lock);
	return arg;
}

static void *block(void *arg)
{
	int fds[2];
	char c;
	pipe(fds);
	read(fds[0], &c, 1);
	return arg;
}

static int threads(void)
{
	void *(*bodies[])(void *) = {spin, spin, doze, wait_futex, block};
	pthread_t t;
	for (unsigned i = 0; i < sizeof bodies / sizeof *bodies; i++)
		pthread_create(&t, 0, bodies[i], 0);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc > 1 && strcmp(argv[1], "threads") == 0)
		return threads();
	int ok = 1;
	for (int i = 0; i < 200; i++) {
		char *args[] = {"/t/main", "threads", NULL};
		pid_t pid;
		int status;
		if (posix_spawn(&pid, "/t/main", NULL, NULL, args, environ) != 0 ||
		    waitpid(pid, &status, 0) != pid) {
			perror("spawn");
			return 1;
		}
		if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
			printf("run %d: status %#x\n", i, status);
			ok = 0;
		}
	}
	printf("200 runs: all exited 0=%d\n", ok);
	return 0;
}
