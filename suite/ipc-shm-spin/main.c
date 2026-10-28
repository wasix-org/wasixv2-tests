// SysV shared memory without synchronisation points: a process started with posix_spawn attaches
// the segment on its own, and the two hand data back and forth by spinning on words in it, with
// no IPC syscall in between, then wake each other with a futex in it (without
// FUTEX_PRIVATE_FLAG). A fork child inherits the attachment, which counts in shm_nattch until it
// detaches, and spins with its parent the same way; a second attachment in one process is the
// same memory.
//#BuildEnv: WASIXCC_WASM_EXCEPTIONS=no
//#MappedDirectory: .:/t
//#ExpectedStdout: spawned ping-pong ok
//#ExpectedStdout: futex ok
//#ExpectedStdout: forked ping-pong ok
//#ExpectedStdout: second attachment ok
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* <linux/futex.h> is not part of the sysroot; asm-generic values. */
#define FUTEX_WAIT 0
#define FUTEX_WAKE 1

extern char **environ;

#define ROUNDS 500

struct shared {
	int turn;
	int futex;
	int waiting;
	int woken;
	int forked;
	char text[64];
};

static int fails;
#define CHECK(c) do { if (!(c)) { printf("%d line %d: CHECK(%s) failed (errno %s)\n", getpid(), __LINE__, #c, strerror(errno)); fails++; } } while (0)

static double now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* Spins until *p holds value, with nothing but loads; gives up after 10 seconds. */
static int spin_until(int *p, int value)
{
	double deadline = now() + 10;
	for (unsigned long i = 1;; i++) {
		if (__atomic_load_n(p, __ATOMIC_ACQUIRE) == value)
			return 0;
		if (i % (1 << 20) == 0 && now() > deadline)
			return -1;
	}
}

static void set(int *p, int value)
{
	__atomic_store_n(p, value, __ATOMIC_RELEASE);
}

/* Plays the odd turns from `first`: waits for each, answers with the next. */
static int answer(int *turn, int first)
{
	for (int i = 0; i < ROUNDS; i++) {
		if (spin_until(turn, first + 2 * i))
			return -1;
		set(turn, first + 2 * i + 1);
	}
	return 0;
}

/* Plays the even turns from `first`. */
static int ask(int *turn, int first)
{
	for (int i = 0; i < ROUNDS; i++) {
		set(turn, first + 2 * i);
		if (spin_until(turn, first + 2 * i + 1))
			return -1;
	}
	return 0;
}

static int spawned(int id)
{
	struct shared *s = shmat(id, NULL, 0);
	CHECK(s != (void *)-1);
	CHECK(strcmp(s->text, "from parent") == 0);
	CHECK(answer(&s->turn, 1) == 0);
	set(&s->waiting, 1);
	long r = syscall(SYS_futex, &s->futex, FUTEX_WAIT, 0, NULL, NULL, 0);
	CHECK(r == 0 || (r == -1 && errno == EAGAIN));
	set(&s->woken, 1);
	CHECK(shmdt(s) == 0);
	return fails != 0;
}

static unsigned long nattch(int id)
{
	struct shmid_ds ds;
	CHECK(shmctl(id, IPC_STAT, &ds) == 0);
	return ds.shm_nattch;
}

int main(int argc, char **argv)
{
	if (argc > 2)
		return spawned(atoi(argv[2]));

	int id = shmget(IPC_PRIVATE, 65536, IPC_CREAT | 0600);
	CHECK(id >= 0);
	struct shared *s = shmat(id, NULL, 0);
	CHECK(s != (void *)-1);
	strcpy(s->text, "from parent");

	char ids[16];
	snprintf(ids, sizeof ids, "%d", id);
	char *args[] = {"/t/main", "spawned", ids, NULL};
	pid_t pid;
	CHECK(posix_spawn(&pid, "/t/main", NULL, NULL, args, environ) == 0);
	if (ask(&s->turn, 1) == 0) {
		printf("spawned ping-pong ok\n");
	} else {
		CHECK(!"the spawned process never answered");
		kill(pid, SIGKILL);
	}
	CHECK(spin_until(&s->waiting, 1) == 0);
	set(&s->futex, 1);
	double deadline = now() + 10;
	while (!__atomic_load_n(&s->woken, __ATOMIC_ACQUIRE) && now() < deadline) {
		syscall(SYS_futex, &s->futex, FUTEX_WAKE, 1, NULL, NULL, 0);
		usleep(1000);
	}
	if (__atomic_load_n(&s->woken, __ATOMIC_ACQUIRE))
		printf("futex ok\n");
	else
		kill(pid, SIGKILL);
	int status;
	CHECK(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0);
	CHECK(nattch(id) == 1);

	set(&s->turn, 0);
	pid_t child = fork();
	if (child == 0) {
		/* The attachment came along: it counts, and it is the same memory. */
		set(&s->forked, 1);
		int bad = answer(&s->turn, 1) != 0 || shmdt(s) != 0;
		_exit(bad || fails);
	}
	CHECK(child > 0);
	CHECK(spin_until(&s->forked, 1) == 0);
	CHECK(nattch(id) >= 1);
	if (ask(&s->turn, 1) == 0)
		printf("forked ping-pong ok\n");
	else
		kill(child, SIGKILL);
	CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
	CHECK(nattch(id) == 1);

	struct shared *t = shmat(id, NULL, 0);
	CHECK(t != (void *)-1 && t != s);
	strcpy(t->text, "through the second");
	if (strcmp(s->text, "through the second") == 0)
		printf("second attachment ok\n");
	CHECK(shmdt(t) == 0 && shmdt(s) == 0);
	CHECK(shmctl(id, IPC_RMID, NULL) == 0);
	printf(fails ? "FAIL\n" : "PASS\n");
	return fails != 0;
}
