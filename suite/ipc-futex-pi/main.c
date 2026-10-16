// Priority-inheritance futexes: the raw FUTEX_LOCK_PI/TRYLOCK_PI/UNLOCK_PI protocol on the
// futex word (owner TID, FUTEX_WAITERS, FUTEX_OWNER_DIED, EDEADLK, EPERM, ESRCH), a lock handed
// to a waiting thread, FUTEX_CMP_REQUEUE_PI from a FUTEX_WAIT_REQUEUE_PI waiter, robust PI
// mutexes whose owner thread exits, and pthread's PTHREAD_PRIO_INHERIT mutexes under contention.
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

/* <linux/futex.h> is not part of the sysroot; asm-generic values. */
#define FUTEX_WAKE 1
#define FUTEX_LOCK_PI 6
#define FUTEX_UNLOCK_PI 7
#define FUTEX_TRYLOCK_PI 8
#define FUTEX_WAIT_REQUEUE_PI 11
#define FUTEX_CMP_REQUEUE_PI 12
#define FUTEX_PRIVATE_FLAG 128
#define FUTEX_WAITERS 0x80000000u
#define FUTEX_OWNER_DIED 0x40000000u

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: CHECK(%s) failed (errno %s)\n", __LINE__, #c, strerror(errno)); fails++; } } while (0)
#define FAILS(call, e) do { errno = 0; long r_ = (long)(call); if (r_ != -1 || errno != (e)) { fprintf(stderr, "line %d: %s = %ld (%s), want -1 %s\n", __LINE__, #call, r_, strerror(errno), #e); fails++; } } while (0)

static long futex(volatile uint32_t *addr, int op, uint32_t val, long val2, volatile uint32_t *addr2, uint32_t val3)
{
	return syscall(SYS_futex, addr, op | FUTEX_PRIVATE_FLAG, val, val2, addr2, val3);
}

static volatile uint32_t lock, cond;
static volatile int waiter_tid, holder_done;

static void *contender(void *arg)
{
	(void)arg;
	waiter_tid = gettid();
	CHECK(futex(&lock, FUTEX_LOCK_PI, 0, 0, NULL, 0) == 0);
	CHECK((lock & 0x3fffffff) == (uint32_t)gettid());
	CHECK(futex(&lock, FUTEX_UNLOCK_PI, 0, 0, NULL, 0) == 0);
	return NULL;
}

static void *requeue_waiter(void *arg)
{
	(void)arg;
	waiter_tid = gettid();
	CHECK(futex(&cond, FUTEX_WAIT_REQUEUE_PI, 0, 0, &lock, 0) == 0);
	CHECK((lock & 0x3fffffff) == (uint32_t)gettid());
	CHECK(futex(&lock, FUTEX_UNLOCK_PI, 0, 0, NULL, 0) == 0);
	return NULL;
}

static pthread_mutex_t robust;

static void *die_holding(void *arg)
{
	(void)arg;
	CHECK(pthread_mutex_lock(&robust) == 0);
	return NULL;
}

static pthread_mutex_t pi;
static long counter;

static void *count(void *arg)
{
	(void)arg;
	for (int i = 0; i < 2000; i++) {
		CHECK(pthread_mutex_lock(&pi) == 0);
		long v = counter;
		counter = v + 1;
		CHECK(pthread_mutex_unlock(&pi) == 0);
	}
	return NULL;
}

/* Waits until the futex word says somebody waits in the kernel. */
static void await_waiters(volatile uint32_t *word)
{
	for (int i = 0; i < 2000 && !(*word & FUTEX_WAITERS); i++)
		usleep(1000);
}

int main(void)
{
	uint32_t me = gettid();

	/* The raw protocol. */
	CHECK(futex(&lock, FUTEX_LOCK_PI, 0, 0, NULL, 0) == 0 && lock == me);
	FAILS(futex(&lock, FUTEX_LOCK_PI, 0, 0, NULL, 0), EDEADLK);
	FAILS(futex(&lock, FUTEX_TRYLOCK_PI, 0, 0, NULL, 0), EDEADLK);
	CHECK(futex(&lock, FUTEX_UNLOCK_PI, 0, 0, NULL, 0) == 0 && lock == 0);
	FAILS(futex(&lock, FUTEX_UNLOCK_PI, 0, 0, NULL, 0), EPERM);
	CHECK(futex(&lock, FUTEX_TRYLOCK_PI, 0, 0, NULL, 0) == 0 && lock == me);
	lock = 0;
	lock = FUTEX_OWNER_DIED;
	CHECK(futex(&lock, FUTEX_TRYLOCK_PI, 0, 0, NULL, 0) == 0 && lock == (FUTEX_OWNER_DIED | me));
	lock = 0x3ffffff0; /* no such task */
	FAILS(futex(&lock, FUTEX_LOCK_PI, 0, 0, NULL, 0), ESRCH);
	lock = 0;

	/* Unlocking hands the lock to the waiter. */
	CHECK(futex(&lock, FUTEX_LOCK_PI, 0, 0, NULL, 0) == 0);
	pthread_t t;
	CHECK(pthread_create(&t, NULL, contender, NULL) == 0);
	await_waiters(&lock);
	CHECK(lock == (me | FUTEX_WAITERS));
	CHECK(futex(&lock, FUTEX_UNLOCK_PI, 0, 0, NULL, 0) == 0);
	CHECK(pthread_join(t, NULL) == 0);
	CHECK(lock == 0);

	/* FUTEX_CMP_REQUEUE_PI takes the free lock for the waiter. */
	CHECK(pthread_create(&t, NULL, requeue_waiter, NULL) == 0);
	for (int i = 0; i < 1000; i++) {
		long r = futex(&cond, FUTEX_CMP_REQUEUE_PI, 1, 0, &lock, 0);
		if (r == 1)
			break;
		CHECK(r == 0);
		usleep(1000);
	}
	FAILS(futex(&cond, FUTEX_CMP_REQUEUE_PI, 2, 0, &lock, 0), EINVAL);
	CHECK(pthread_join(t, NULL) == 0);
	CHECK(lock == 0);

	/* A robust PI mutex whose owner exits: the next locker gets EOWNERDEAD. */
	pthread_mutexattr_t ma;
	pthread_mutexattr_init(&ma);
	pthread_mutexattr_setrobust(&ma, PTHREAD_MUTEX_ROBUST);
	pthread_mutexattr_setprotocol(&ma, PTHREAD_PRIO_INHERIT);
	CHECK(pthread_mutex_init(&robust, &ma) == 0);
	CHECK(pthread_create(&t, NULL, die_holding, NULL) == 0);
	CHECK(pthread_join(t, NULL) == 0);
	CHECK(pthread_mutex_lock(&robust) == EOWNERDEAD);
	CHECK(pthread_mutex_consistent(&robust) == 0);
	CHECK(pthread_mutex_unlock(&robust) == 0);
	CHECK(pthread_mutex_lock(&robust) == 0);
	CHECK(pthread_mutex_unlock(&robust) == 0);

	/* PTHREAD_PRIO_INHERIT mutexes exclude each other under contention. */
	pthread_mutexattr_t pa;
	pthread_mutexattr_init(&pa);
	pthread_mutexattr_setprotocol(&pa, PTHREAD_PRIO_INHERIT);
	CHECK(pthread_mutex_init(&pi, &pa) == 0);
	pthread_t ts[4];
	for (int i = 0; i < 4; i++)
		CHECK(pthread_create(&ts[i], NULL, count, NULL) == 0);
	for (int i = 0; i < 4; i++)
		CHECK(pthread_join(ts[i], NULL) == 0);
	CHECK(counter == 8000);

	printf(fails ? "FAIL\n" : "PASS\n");
	return fails != 0;
}
