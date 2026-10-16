// pthread_cancel acts only at cancellation points, with musl's semantics: SIGCANCEL is
// installed with SA_RESTART, so a blocking call it interrupts restarts unless it is an enabled
// cancellation point.
// - A cancellation point with cancellation disabled keeps waiting and returns its data.
// - A blocking syscall that is no cancellation point (a raw FUTEX_WAIT) restarts and returns 0.
// - An enabled cancellation point blocked in read is cancelled; the read never returns.
// - With PTHREAD_CANCEL_MASKED, the blocked read fails with ECANCELED instead.
//#ExpectedStdout: disabled read: n=1 c=a canceled=1
//#ExpectedStdout: futex wait: r=0 canceled=1
//#ExpectedStdout: blocked read: returned=0 cleanup=1 canceled=1
//#ExpectedStdout: masked read: n=-1 ecanceled=1 canceled=0
//#ExpectedExitCode: 0
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#define FUTEX_WAIT_PRIVATE 128
#define FUTEX_WAKE_PRIVATE 129

static int fds[2];
static int ready, word, cleanup, returned;
static long result;
static int saved_errno;
static char byte;

static void pause_ms(int ms)
{
	struct timespec ts = {0, ms * 1000L * 1000L};
	nanosleep(&ts, 0);
}

static void set_ready(void)
{
	__atomic_store_n(&ready, 1, __ATOMIC_SEQ_CST);
}

static void *disabled_read(void *arg)
{
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, 0);
	set_ready();
	result = read(fds[0], &byte, 1);
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, 0);
	pthread_testcancel();
	return 0;
}

static void *futex_wait(void *arg)
{
	set_ready();
	result = syscall(SYS_futex, &word, FUTEX_WAIT_PRIVATE, 0, 0);
	saved_errno = errno;
	pthread_testcancel();
	return 0;
}

static void on_cancel(void *arg)
{
	cleanup = 1;
}

static void *blocked_read(void *arg)
{
	pthread_cleanup_push(on_cancel, 0);
	set_ready();
	read(fds[0], &byte, 1);
	returned = 1;
	pthread_cleanup_pop(0);
	return 0;
}

static void *masked_read(void *arg)
{
	pthread_setcancelstate(PTHREAD_CANCEL_MASKED, 0);
	set_ready();
	result = read(fds[0], &byte, 1);
	saved_errno = errno;
	return 0;
}

// Starts f, cancels it once it is blocked, then runs release after a while and joins it.
static int cancel_blocked(void *(*f)(void *), void (*release)(void))
{
	pthread_t t;
	__atomic_store_n(&ready, 0, __ATOMIC_SEQ_CST);
	if (pthread_create(&t, 0, f, 0)) {
		perror("pthread_create");
		return -1;
	}
	while (!__atomic_load_n(&ready, __ATOMIC_SEQ_CST))
		;
	pause_ms(100);
	pthread_cancel(t);
	pause_ms(100);
	if (release) release();
	void *ret;
	pthread_join(t, &ret);
	return ret == PTHREAD_CANCELED;
}

static void write_byte(void)
{
	write(fds[1], "a", 1);
}

static void wake_word(void)
{
	__atomic_store_n(&word, 1, __ATOMIC_SEQ_CST);
	syscall(SYS_futex, &word, FUTEX_WAKE_PRIVATE, 1);
}

int main(void)
{
	if (pipe(fds)) {
		perror("pipe");
		return 1;
	}

	int canceled = cancel_blocked(disabled_read, write_byte);
	printf("disabled read: n=%ld c=%c canceled=%d\n", result, result == 1 ? byte : '-', canceled);

	canceled = cancel_blocked(futex_wait, wake_word);
	printf("futex wait: r=%ld canceled=%d\n", result, canceled);
	if (result) printf("futex wait: errno=%d\n", saved_errno);

	canceled = cancel_blocked(blocked_read, 0);
	printf("blocked read: returned=%d cleanup=%d canceled=%d\n", returned, cleanup, canceled);

	canceled = cancel_blocked(masked_read, 0);
	printf("masked read: n=%ld ecanceled=%d canceled=%d\n", result, saved_errno == ECANCELED,
	       canceled);
	return 0;
}
