// A thread blocked in FUTEX_LOCK_PI runs a signal handler without SA_RESTART and goes on
// waiting: PI futex waits restart after any handler (ERESTARTNOINTR), so the raw syscall never
// fails with EINTR and returns 0 once the owner unlocks. pthread_mutex_lock on a
// PTHREAD_PRIO_INHERIT mutex, which musl retries on EINTR anyway, gets the lock the same way.
//#ExpectedStdout: handled=1 locked=1 owner_ok=1 raw_restarted=1
//#ExpectedExitCode: 0
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#define FUTEX_LOCK_PI 6
#define FUTEX_UNLOCK_PI 7
#define FUTEX_PRIVATE_FLAG 128

static pthread_mutex_t mutex;
static volatile sig_atomic_t handled;
static volatile int locked, waiting;
static volatile uint32_t raw;
static volatile long raw_result = -2;

static void on_usr1(int sig)
{
	(void)sig;
	handled++;
}

static void pause_ms(int ms)
{
	struct timespec d = {0, ms * 1000 * 1000};
	nanosleep(&d, 0);
}

static void *contender(void *arg)
{
	(void)arg;
	waiting = 1;
	if (pthread_mutex_lock(&mutex) == 0) {
		locked = 1;
		pthread_mutex_unlock(&mutex);
	}
	return 0;
}

static void *raw_contender(void *arg)
{
	(void)arg;
	raw_result = syscall(SYS_futex, &raw, FUTEX_LOCK_PI | FUTEX_PRIVATE_FLAG, 0, 0, 0, 0);
	if (raw_result == 0)
		syscall(SYS_futex, &raw, FUTEX_UNLOCK_PI | FUTEX_PRIVATE_FLAG, 0, 0, 0, 0);
	return 0;
}

int main(void)
{
	struct sigaction sa = {0};
	sa.sa_handler = on_usr1;
	sigaction(SIGUSR1, &sa, 0);
	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT);
	pthread_mutex_init(&mutex, &attr);

	pthread_mutex_lock(&mutex);
	pthread_t t;
	pthread_create(&t, 0, contender, 0);
	while (!waiting) pause_ms(5);
	pause_ms(50);
	pthread_kill(t, SIGUSR1);
	pause_ms(50);
	int early = locked;
	pthread_mutex_unlock(&mutex);
	pthread_join(t, 0);
	printf("handled=%d locked=%d owner_ok=%d ", (int)handled, locked, !early);

	/* The raw syscall shows the restart: it is still waiting after the handler. */
	raw = (uint32_t)gettid();
	pthread_t r;
	pthread_create(&r, 0, raw_contender, 0);
	pause_ms(50);
	pthread_kill(r, SIGUSR1);
	pause_ms(50);
	int still_waiting = raw_result == -2;
	syscall(SYS_futex, &raw, FUTEX_UNLOCK_PI | FUTEX_PRIVATE_FLAG, 0, 0, 0, 0);
	pthread_join(r, 0);
	printf("raw_restarted=%d\n", still_waiting && raw_result == 0);
	return 0;
}
