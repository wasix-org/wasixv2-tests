// Process-shared pthread objects in anonymous or file-backed MAP_SHARED memory across fork, as the Open
// POSIX tests of PTHREAD_PROCESS_SHARED use them: parent and child take turns under a mutex and
// a condition variable, both count under a contended mutex and a priority-inheritance mutex, and
// a sem_init(pshared = 1) semaphore passes a token back and forth.
//#BuildEnv: WASIXCC_WASM_EXCEPTIONS=no
//#ExpectedStdout: condvar turns ok
//#ExpectedStdout: mutex count 40000 ok
//#ExpectedStdout: pi mutex count 40000 ok
//#ExpectedStdout: semaphore ok
//#ExpectedStdout: PASS
//#Config: tmpfile:default
//#Args: tmpfile
//#Config: rootfile:default
//#Args: rootfile
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define TURNS 200
#define COUNT 20000
#define TOKENS 200

struct shared {
	pthread_mutex_t lock;
	pthread_cond_t cond;
	int turn;
	pthread_mutex_t counting;
	pthread_mutex_t pi;
	long count;
	long pi_count;
	sem_t to_child, to_parent;
};

static struct shared *s;
static int fails;
#define CHECK(c) do { if (!(c)) { printf("%d line %d: CHECK(%s) failed (errno %s)\n", getpid(), __LINE__, #c, strerror(errno)); fails++; } } while (0)

/* Waits under the condition variable until the turn is `want`, for at most 10 seconds. */
static int wait_turn(int want)
{
	struct timespec deadline;
	clock_gettime(CLOCK_REALTIME, &deadline);
	deadline.tv_sec += 10;
	int err = 0;
	pthread_mutex_lock(&s->lock);
	while (s->turn != want && err == 0)
		err = pthread_cond_timedwait(&s->cond, &s->lock, &deadline);
	pthread_mutex_unlock(&s->lock);
	return s->turn == want ? 0 : -1;
}

static void give_turn(int turn)
{
	pthread_mutex_lock(&s->lock);
	s->turn = turn;
	pthread_cond_broadcast(&s->cond);
	pthread_mutex_unlock(&s->lock);
}

static void count(void)
{
	for (int i = 0; i < COUNT; i++) {
		pthread_mutex_lock(&s->counting);
		s->count++;
		pthread_mutex_unlock(&s->counting);
		pthread_mutex_lock(&s->pi);
		s->pi_count++;
		pthread_mutex_unlock(&s->pi);
	}
}

static int timed_wait(sem_t *sem)
{
	struct timespec deadline;
	clock_gettime(CLOCK_REALTIME, &deadline);
	deadline.tv_sec += 10;
	return sem_timedwait(sem, &deadline);
}

static int child(void)
{
	for (int i = 0; i < TURNS; i++) {
		if (wait_turn(2 * i + 1))
			return 1;
		give_turn(2 * i + 2);
	}
	count();
	for (int i = 0; i < TOKENS; i++) {
		if (timed_wait(&s->to_child))
			return 1;
		CHECK(sem_post(&s->to_parent) == 0);
	}
	return fails != 0;
}

static void init(void)
{
	pthread_mutexattr_t ma;
	pthread_condattr_t ca;
	CHECK(pthread_mutexattr_init(&ma) == 0);
	CHECK(pthread_mutexattr_setpshared(&ma, PTHREAD_PROCESS_SHARED) == 0);
	CHECK(pthread_mutex_init(&s->lock, &ma) == 0);
	CHECK(pthread_mutex_init(&s->counting, &ma) == 0);
	CHECK(pthread_mutexattr_setprotocol(&ma, PTHREAD_PRIO_INHERIT) == 0);
	CHECK(pthread_mutex_init(&s->pi, &ma) == 0);
	CHECK(pthread_condattr_init(&ca) == 0);
	CHECK(pthread_condattr_setpshared(&ca, PTHREAD_PROCESS_SHARED) == 0);
	CHECK(pthread_cond_init(&s->cond, &ca) == 0);
	CHECK(sem_init(&s->to_child, 1, 0) == 0);
	CHECK(sem_init(&s->to_parent, 1, 0) == 0);
}

int main(int argc, char **argv)
{
	int fd = -1;
	int flags = MAP_SHARED | MAP_ANONYMOUS;
	if (argc > 1) {
		const char *name = !strcmp(argv[1], "tmpfile") ? "/tmp/pshared" : "/pshared";
		/* mm/shmem.c shmem_mmap_prepare and kernel/futex/core.c get_futex_key: a tmpfs
		 * file supplies the same shared pages and futex key in both processes. */
		fd = open(name, O_CREAT | O_EXCL | O_RDWR, 0600);
		CHECK(fd >= 0);
		CHECK(unlink(name) == 0);
		CHECK(ftruncate(fd, sizeof *s) == 0);
		flags = MAP_SHARED;
	}
	s = mmap(NULL, sizeof *s, PROT_READ | PROT_WRITE, flags, fd, 0);
	CHECK(s != MAP_FAILED);
	init();

	pid_t pid = fork();
	if (pid == 0)
		_exit(child());
	CHECK(pid > 0);

	int ok = 1;
	for (int i = 0; i < TURNS && ok; i++) {
		give_turn(2 * i + 1);
		ok = wait_turn(2 * i + 2) == 0;
	}
	if (!ok) {
		printf("the child never took its turn\n");
		kill(pid, SIGKILL);
		waitpid(pid, NULL, 0);
		printf("FAIL\n");
		return 1;
	}
	printf("condvar turns ok\n");

	count();
	for (int i = 0; i < TOKENS && ok; i++) {
		CHECK(sem_post(&s->to_child) == 0);
		ok = timed_wait(&s->to_parent) == 0;
	}
	if (!ok)
		kill(pid, SIGKILL);
	int status;
	CHECK(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0);

	pthread_mutex_lock(&s->counting);
	long counted = s->count;
	pthread_mutex_unlock(&s->counting);
	if (counted == 2 * COUNT)
		printf("mutex count %ld ok\n", counted);
	else
		CHECK(counted == 2 * COUNT);
	if (s->pi_count == 2 * COUNT)
		printf("pi mutex count %ld ok\n", s->pi_count);
	else
		CHECK(s->pi_count == 2 * COUNT);
	if (ok)
		printf("semaphore ok\n");
	printf(fails ? "FAIL\n" : "PASS\n");
	return fails != 0;
}
