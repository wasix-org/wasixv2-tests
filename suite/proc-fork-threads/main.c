// fork in a program with threads, as POSIX has it: the child has one thread, the one that called
// fork, so the parent's other threads do not run there; the child can start threads of its own;
// and a thread other than the main one can fork, its child running on in that thread's code.
//#BuildEnv: WASIXCC_WASM_EXCEPTIONS=no
//#ExpectedStdout: child: others-stopped=1 own-thread=21
//#ExpectedStdout: parent: status=0 others-running=1
//#ExpectedStdout: thread child: in-thread=1
//#ExpectedStdout: thread: status=4
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static volatile int stop;
static volatile unsigned long ticks;

static void *spin(void *arg)
{
	(void)arg;
	while (!stop) __atomic_fetch_add(&ticks, 1, __ATOMIC_RELAXED);
	return NULL;
}

static void *answer(void *arg)
{
	return (void *)((long)arg + 1);
}

static void nap(void)
{
	struct timespec ts = {0, 50 * 1000 * 1000};
	nanosleep(&ts, NULL);
}

static __thread int in_thread;

static void *forker(void *arg)
{
	(void)arg;
	in_thread = 1;
	fflush(stdout);
	pid_t pid = fork();
	if (pid == 0) {
		printf("thread child: in-thread=%d\n", in_thread);
		fflush(stdout);
		_exit(4);
	}
	int status = -1;
	if (pid < 0 || waitpid(pid, &status, 0) != pid) return (void *)-1L;
	return (void *)(long)WEXITSTATUS(status);
}

int main(void)
{
	pthread_t spinners[2];
	for (int i = 0; i < 2; i++) pthread_create(&spinners[i], NULL, spin, NULL);
	while (__atomic_load_n(&ticks, __ATOMIC_RELAXED) == 0) nap();

	fflush(stdout);
	pid_t pid = fork();
	if (pid < 0) {
		perror("fork");
		return 1;
	}
	if (pid == 0) {
		unsigned long before = ticks;
		nap();
		int stopped = ticks == before;
		pthread_t t;
		void *ret = NULL;
		if (pthread_create(&t, NULL, answer, (void *)20L) != 0 || pthread_join(t, &ret) != 0)
			_exit(1);
		printf("child: others-stopped=%d own-thread=%ld\n", stopped, (long)ret);
		fflush(stdout);
		_exit(0);
	}
	int status;
	if (waitpid(pid, &status, 0) != pid) {
		perror("waitpid");
		return 1;
	}
	unsigned long before = ticks;
	nap();
	printf("parent: status=%d others-running=%d\n", WEXITSTATUS(status), ticks != before);
	stop = 1;
	for (int i = 0; i < 2; i++) pthread_join(spinners[i], NULL);

	pthread_t t;
	void *ret;
	pthread_create(&t, NULL, forker, NULL);
	pthread_join(t, &ret);
	printf("thread: status=%ld\n", (long)ret);
	return 0;
}
