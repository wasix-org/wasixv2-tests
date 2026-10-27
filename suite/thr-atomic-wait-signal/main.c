// The wasm instruction memory.atomic.wait32 is a futex wait: a signal interrupts it and its
// handler runs, after which an untimed wait goes on whatever the handler's SA_RESTART, since the
// instruction cannot fail (ERESTARTNOINTR); it returns only once woken. FUTEX_WAKE ends an
// instruction wait, memory.atomic.notify a FUTEX_WAIT, and exit ends a thread blocked in the
// instruction.
//#ExpectedStdout: norestart=0 handled=1 word=1 restart=0 handled=2 word=2 notified=1 timeout=2 mismatch=1
//#ExpectedExitCode: 0
#define _GNU_SOURCE
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

/* <linux/futex.h> is not part of the sysroot; asm-generic values. */
#define FUTEX_WAIT_PRIVATE 128
#define FUTEX_WAKE_PRIVATE 129

static int word, other, stuck, step;
static volatile sig_atomic_t handled;
static pthread_t main_thread;

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

static int wait32(int *addr, int expected, long long timeout)
{
	return __builtin_wasm_memory_atomic_wait32(addr, expected, timeout);
}

static void *signaller(void *arg)
{
	(void)arg;
	// Interrupts the wait whose handler has no SA_RESTART, then ends it.
	pause_ms(50);
	pthread_kill(main_thread, SIGUSR1);
	pause_ms(50);
	__atomic_store_n(&word, 1, __ATOMIC_SEQ_CST);
	syscall(SYS_futex, &word, FUTEX_WAKE_PRIVATE, 1, 0, 0, 0);
	// The same for the restarting wait.
	while (__atomic_load_n(&step, __ATOMIC_SEQ_CST) < 1) pause_ms(5);
	pause_ms(50);
	pthread_kill(main_thread, SIGUSR1);
	pause_ms(50);
	__atomic_store_n(&word, 2, __ATOMIC_SEQ_CST);
	syscall(SYS_futex, &word, FUTEX_WAKE_PRIVATE, 1, 0, 0, 0);
	// A FUTEX_WAIT that only the instruction's notify ends.
	syscall(SYS_futex, &other, FUTEX_WAIT_PRIVATE, 0, 0, 0, 0);
	return 0;
}

static void *blocked(void *arg)
{
	(void)arg;
	wait32(&stuck, 0, -1);
	return 0;
}

int main(void)
{
	main_thread = pthread_self();
	struct sigaction sa = {0};
	sa.sa_handler = on_usr1;
	sigaction(SIGUSR1, &sa, 0);
	pthread_t t, b;
	pthread_create(&t, 0, signaller, 0);
	int r = wait32(&word, 0, -1);
	printf("norestart=%d handled=%d word=%d ", r, (int)handled, word);

	sa.sa_flags = SA_RESTART;
	sigaction(SIGUSR1, &sa, 0);
	__atomic_store_n(&step, 1, __ATOMIC_SEQ_CST);
	r = wait32(&word, 1, -1);
	printf("restart=%d handled=%d word=%d ", r, (int)handled, word);

	int notified;
	while (!(notified = __builtin_wasm_memory_atomic_notify(&other, 1))) pause_ms(5);
	pthread_join(t, 0);
	printf("notified=%d ", notified);
	printf("timeout=%d mismatch=%d\n", wait32(&word, 2, 1000000), wait32(&word, 0, -1));

	pthread_create(&b, 0, blocked, 0);
	pause_ms(20);
	fflush(stdout);
	return 0;
}
