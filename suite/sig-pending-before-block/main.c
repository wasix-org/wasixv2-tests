// A process signal that only one thread takes, sent while that thread runs guest code, reaches
// it before that thread's next sigprocmask blocks it: on Linux the sender's kill kicks the
// running thread, which takes the signal on its way back to user mode, before its next syscall.
// A thread on its way out of pthread_exit blocks every signal first; a signal it hid would be
// stranded, with nobody to take it (pthread_once/6-1 hung on that).
//#ExpectedStdout: rounds 100 handled 100 stranded 0
//#ExpectedExitCode: 0
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#define ROUNDS 100

static atomic_int asked, sent, handled;

static void handler(int sig)
{
	(void)sig;
	atomic_fetch_add(&handled, 1);
}

static void *worker(void *arg)
{
	(void)arg;
	sigset_t usr1;
	sigemptyset(&usr1);
	sigaddset(&usr1, SIGUSR1);
	long stranded = 0;
	for (int round = 1; round <= ROUNDS; round++) {
		pthread_sigmask(SIG_UNBLOCK, &usr1, 0);
		atomic_store(&asked, round);
		/* No syscall until the signal was sent: the thread only runs guest code. */
		while (atomic_load(&sent) < round)
			;
		pthread_sigmask(SIG_BLOCK, &usr1, 0);
		sigset_t pending;
		sigpending(&pending);
		if (sigismember(&pending, SIGUSR1)) {
			int sig;
			stranded++;
			sigwait(&usr1, &sig);
		}
	}
	return (void *)stranded;
}

/* Spins for `us` microseconds, far longer than a kick takes to land on Linux. */
static void spin(long us)
{
	struct timespec start, now;
	clock_gettime(CLOCK_MONOTONIC, &start);
	do
		clock_gettime(CLOCK_MONOTONIC, &now);
	while ((now.tv_sec - start.tv_sec) * 1000000 + (now.tv_nsec - start.tv_nsec) / 1000 < us);
}

int main(void)
{
	sigset_t usr1;
	sigemptyset(&usr1);
	sigaddset(&usr1, SIGUSR1);
	pthread_sigmask(SIG_BLOCK, &usr1, 0);
	signal(SIGUSR1, handler);
	pthread_t t;
	if (pthread_create(&t, 0, worker, 0) != 0)
		return 1;
	for (int round = 1; round <= ROUNDS; round++) {
		while (atomic_load(&asked) < round)
			;
		kill(getpid(), SIGUSR1);
		spin(10);
		atomic_store(&sent, round);
	}
	void *stranded;
	pthread_join(t, &stranded);
	printf("rounds %d handled %d stranded %ld\n", ROUNDS, atomic_load(&handled), (long)stranded);
	return 0;
}
