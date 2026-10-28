// MAP_SHARED|MAP_ANONYMOUS memory is one memory across fork: parent and child see each other's
// stores while they spin with no syscall in between, in both directions and with a grandchild; a
// futex in it (without FUTEX_PRIVATE_FLAG) wakes across the processes; a second mapping of it
// made with mremap(old_size 0) is the same memory; the child's munmap leaves the parent's mapping
// alone; MAP_PRIVATE memory stays each process's own.
//#BuildEnv: WASIXCC_WASM_EXCEPTIONS=no
//#ExpectedStdout: ping-pong 1000 ok
//#ExpectedStdout: grandchild ok
//#ExpectedStdout: futex ok
//#ExpectedStdout: duplicate ok
//#ExpectedStdout: private ok
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* <linux/futex.h> is not part of the sysroot; asm-generic values. */
#define FUTEX_WAIT 0
#define FUTEX_WAKE 1

#define ROUNDS 1000
#define LEN (2 * 65536)

struct shared {
	int turn;
	int grand;
	int futex;
	int waiting;
	int woken;
	char text[64];
};

static struct shared *s;
static int fails;
#define CHECK(c) do { if (!(c)) { printf("%d line %d: CHECK(%s) failed (errno %s)\n", getpid(), __LINE__, #c, strerror(errno)); fails++; } } while (0)

static double now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* Spins until *p holds value, reading it with nothing but loads; gives up after 10 seconds. */
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

static int child(int *priv)
{
	CHECK(strcmp(s->text, "before fork") == 0);
	for (int i = 0; i < ROUNDS; i++) {
		if (spin_until(&s->turn, 2 * i + 1)) {
			printf("child: no turn %d\n", 2 * i + 1);
			return 1;
		}
		set(&s->turn, 2 * i + 2);
	}

	pid_t grand = fork();
	if (grand == 0) {
		set(&s->grand, 1);
		if (spin_until(&s->grand, 2))
			_exit(1);
		set(&s->grand, 3);
		_exit(0);
	}
	int status;
	CHECK(grand > 0 && waitpid(grand, &status, 0) == grand && WIFEXITED(status) && WEXITSTATUS(status) == 0);

	set(&s->waiting, 1);
	long r = syscall(SYS_futex, &s->futex, FUTEX_WAIT, 0, NULL, NULL, 0);
	CHECK(r == 0 || (r == -1 && errno == EAGAIN));
	set(&s->woken, 1);

	*priv = 2;
	CHECK(munmap(s, LEN) == 0);
	return fails != 0;
}

int main(void)
{
	s = mmap(NULL, LEN, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	int *priv = mmap(NULL, 65536, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	CHECK(s != MAP_FAILED && priv != MAP_FAILED);
	strcpy(s->text, "before fork");
	*priv = 1;

	pid_t pid = fork();
	if (pid == 0)
		_exit(child(priv));
	CHECK(pid > 0);

	int ok = 1;
	for (int i = 0; i < ROUNDS && ok; i++) {
		set(&s->turn, 2 * i + 1);
		if (spin_until(&s->turn, 2 * i + 2)) {
			printf("parent: no turn %d\n", 2 * i + 2);
			ok = 0;
		}
	}
	if (!ok) {
		kill(pid, SIGKILL);
		waitpid(pid, NULL, 0);
		printf("FAIL\n");
		return 1;
	}
	printf("ping-pong %d ok\n", ROUNDS);

	if (spin_until(&s->grand, 1) == 0) {
		set(&s->grand, 2);
		CHECK(spin_until(&s->grand, 3) == 0);
		printf("grandchild ok\n");
	} else {
		CHECK(!"the grandchild never showed up");
	}

	/* The child blocks in FUTEX_WAIT on the word; only a wake that reaches it lets it go on. */
	CHECK(spin_until(&s->waiting, 1) == 0);
	set(&s->futex, 1);
	double deadline = now() + 10;
	while (!__atomic_load_n(&s->woken, __ATOMIC_ACQUIRE) && now() < deadline) {
		syscall(SYS_futex, &s->futex, FUTEX_WAKE, 1, NULL, NULL, 0);
		usleep(1000);
	}
	if (__atomic_load_n(&s->woken, __ATOMIC_ACQUIRE)) {
		printf("futex ok\n");
	} else {
		CHECK(!"the wake never reached the child");
		kill(pid, SIGKILL);
	}

	int status;
	CHECK(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0);

	/* A second mapping of the same memory (Linux's "duplicate shm area"). */
	struct shared *d = mremap(s, 0, LEN, MREMAP_MAYMOVE);
	CHECK(d != MAP_FAILED && d != s);
	if (d != MAP_FAILED) {
		strcpy(d->text, "through the duplicate");
		CHECK(strcmp(s->text, "through the duplicate") == 0);
		CHECK(munmap(d, LEN) == 0);
		printf("duplicate ok\n");
	}

	/* The child's munmap took nothing from here, and its private store stayed its own. */
	strcpy(s->text, "after");
	CHECK(strcmp(s->text, "after") == 0);
	CHECK(*priv == 1);
	if (*priv == 1)
		printf("private ok\n");
	printf(fails ? "FAIL\n" : "PASS\n");
	return fails != 0;
}
