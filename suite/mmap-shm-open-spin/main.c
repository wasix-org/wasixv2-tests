// A POSIX shared memory object mapped MAP_SHARED is one memory in every process that maps it: a
// process started with posix_spawn opens it by name and a fork child inherits the mapping, and
// each hands data back and forth with the parent by spinning on words in it, with no syscall in
// between; a futex in it (without FUTEX_PRIVATE_FLAG) wakes across the processes; and read() and
// write() on the object meet the mappings at once, with no msync.
//#BuildEnv: WASIXCC_WASM_EXCEPTIONS=no
//#MappedDirectory: .:/t
//#ExpectedStdout: spawned ping-pong ok
//#ExpectedStdout: futex ok
//#ExpectedStdout: forked ping-pong ok
//#ExpectedStdout: read and write ok
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* <linux/futex.h> is not part of the sysroot; asm-generic values. */
#define FUTEX_WAIT 0
#define FUTEX_WAKE 1

extern char **environ;

#define ROUNDS 500
#define LEN 65536

struct shared {
	int spawned_turn;
	int forked_turn;
	int futex;
	int waiting;
	int woken;
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

/* The other side of a ping-pong on *turn: answers every odd turn with the next even one. */
static int answer(int *turn)
{
	for (int i = 0; i < ROUNDS; i++) {
		if (spin_until(turn, 2 * i + 1))
			return -1;
		set(turn, 2 * i + 2);
	}
	return 0;
}

static int play(int *turn)
{
	for (int i = 0; i < ROUNDS; i++) {
		set(turn, 2 * i + 1);
		if (spin_until(turn, 2 * i + 2))
			return -1;
	}
	return 0;
}

/* Opens the object by name, as an unrelated process would. */
static int spawned(const char *name)
{
	int fd = shm_open(name, O_RDWR, 0);
	if (fd < 0)
		return 2;
	struct shared *s = mmap(NULL, LEN, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	close(fd);
	if (s == MAP_FAILED || strcmp(s->text, "from the parent") != 0)
		return 3;
	if (answer(&s->spawned_turn))
		return 4;
	set(&s->waiting, 1);
	long r = syscall(SYS_futex, &s->futex, FUTEX_WAIT, 0, NULL, NULL, 0);
	if (r != 0 && !(r == -1 && errno == EAGAIN))
		return 5;
	set(&s->woken, 1);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc == 3 && strcmp(argv[1], "spawned") == 0)
		return spawned(argv[2]);

	char name[64];
	snprintf(name, sizeof(name), "/wasixv2-spin-%d", getpid());
	int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
	CHECK(fd >= 0);
	CHECK(ftruncate(fd, LEN) == 0);
	struct shared *s = mmap(NULL, LEN, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (fd < 0 || s == MAP_FAILED) {
		printf("FAIL\n");
		return 1;
	}
	strcpy(s->text, "from the parent");

	pid_t pid;
	char *args[] = {"/t/main", "spawned", name, NULL};
	CHECK(posix_spawn(&pid, "/t/main", NULL, NULL, args, environ) == 0);
	if (play(&s->spawned_turn) == 0)
		printf("spawned ping-pong ok\n");
	else
		CHECK(!"the spawned process never answered");

	/* The spawned process blocks in FUTEX_WAIT; only a wake that reaches it lets it go on. */
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

	/* A fork child keeps the mapping, and writes the object with write() at the end. */
	pid_t child = fork();
	if (child == 0) {
		if (answer(&s->forked_turn))
			_exit(1);
		if (pwrite(fd, "written", 8, offsetof(struct shared, text)) != 8)
			_exit(2);
		_exit(0);
	}
	CHECK(child > 0);
	if (play(&s->forked_turn) == 0)
		printf("forked ping-pong ok\n");
	else
		kill(child, SIGKILL);
	CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);

	/* The child's write() shows in this mapping, and a store here in read(), with no msync. */
	CHECK(strcmp(s->text, "written") == 0);
	strcpy(s->text, "stored");
	char buf[8] = {0};
	CHECK(pread(fd, buf, 7, offsetof(struct shared, text)) == 7);
	CHECK(strcmp(buf, "stored") == 0);
	if (strcmp(s->text, "stored") == 0 && strcmp(buf, "stored") == 0)
		printf("read and write ok\n");

	CHECK(munmap(s, LEN) == 0);
	CHECK(close(fd) == 0);
	CHECK(shm_unlink(name) == 0);
	printf(fails ? "FAIL\n" : "PASS\n");
	return fails != 0;
}
