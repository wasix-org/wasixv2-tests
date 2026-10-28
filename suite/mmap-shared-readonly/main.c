// Read-only views of shared memory: a SHM_RDONLY attachment and a MAP_SHARED PROT_READ mapping of
// a POSIX shared memory object opened O_RDONLY show every store another process makes, with no
// syscall in between, but take none: a store into one kills the process with SIGSEGV (whether it
// attached the view itself or a fork inherited it), read() into one fails with EFAULT and leaves
// it as it was, and mprotect cannot make one writable (EACCES). Unmapped, the range is ordinary
// writable memory again.
//#BuildEnv: WASIXCC_WASM_EXCEPTIONS=no
//#ExpectedStdout: attached store: signaled=1 sig=11
//#ExpectedStdout: inherited store: signaled=1 sig=11
//#ExpectedStdout: read into view: EFAULT
//#ExpectedStdout: mprotect: EACCES
//#ExpectedStdout: detached writable ok
//#ExpectedStdout: file store: signaled=1 sig=11
//#ExpectedStdout: file ping-pong ok
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define ROUNDS 500
#define LEN 65536

static int fails;
#define CHECK(c) do { if (!(c)) { printf("%d line %d: CHECK(%s) failed (errno %s)\n", getpid(), __LINE__, #c, strerror(errno)); fails++; } } while (0)

static double now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* Spins until *p holds value, reading it with nothing but loads; gives up after 10 seconds. */
static int spin_until(volatile int *p, int value)
{
	double deadline = now() + 10;
	for (unsigned long i = 1;; i++) {
		if (__atomic_load_n(p, __ATOMIC_ACQUIRE) == value)
			return 0;
		if (i % (1 << 20) == 0 && now() > deadline)
			return -1;
	}
}

/* Forks a child that runs `store` and reports how it ended. */
static void expect_segv(const char *what, void (*store)(void *), void *arg)
{
	fflush(stdout);
	pid_t pid = fork();
	if (pid == 0) {
		store(arg);
		_exit(0);
	}
	int status;
	CHECK(pid > 0 && waitpid(pid, &status, 0) == pid);
	printf("%s: signaled=%d sig=%d\n", what, WIFSIGNALED(status),
	       WIFSIGNALED(status) ? WTERMSIG(status) : -1);
}

static void store_to(void *p)
{
	*(volatile int *)p = 1;
}

static void attach_and_store(void *id)
{
	char *view = shmat(*(int *)id, NULL, SHM_RDONLY);
	if (view == (void *)-1)
		_exit(2);
	store_to(view + 8);
}

static void segments(void)
{
	int id = shmget(IPC_PRIVATE, LEN, IPC_CREAT | 0600);
	CHECK(id >= 0);
	char *rw = shmat(id, NULL, 0);
	char *ro = shmat(id, NULL, SHM_RDONLY);
	CHECK(rw != (void *)-1 && ro != (void *)-1);

	expect_segv("attached store", attach_and_store, &id);
	expect_segv("inherited store", store_to, ro + 16);

	strcpy(rw, "shown");
	CHECK(strcmp(ro, "shown") == 0);
	int fds[2];
	CHECK(pipe(fds) == 0);
	CHECK(write(fds[1], "pipe", 4) == 4);
	errno = 0;
	ssize_t n = read(fds[0], ro, 4);
	printf("read into view: %s\n", n == -1 && errno == EFAULT ? "EFAULT" : "stored");
	CHECK(strcmp(ro, "shown") == 0);
	close(fds[0]);
	close(fds[1]);
	errno = 0;
	int r = mprotect(ro, LEN, PROT_READ | PROT_WRITE);
	printf("mprotect: %s\n", r == -1 && errno == EACCES ? "EACCES" : "allowed");

	/* Detached, the range is private writable memory; mapped there again, it is ours. */
	CHECK(shmdt(ro) == 0);
	char *again = mmap(ro, LEN, PROT_READ | PROT_WRITE,
			   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
	CHECK(again == ro);
	again[0] = 'x';
	CHECK(strcmp(rw, "shown") == 0);
	printf("detached writable ok\n");
	CHECK(shmdt(rw) == 0);
	CHECK(shmctl(id, IPC_RMID, NULL) == 0);
}

static void files(void)
{
	char name[64];
	snprintf(name, sizeof name, "/mmap-shared-readonly-%d", getpid());
	int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
	CHECK(fd >= 0);
	CHECK(ftruncate(fd, LEN) == 0);
	volatile int *rw = mmap(NULL, LEN, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	CHECK(rw != MAP_FAILED);
	/* The child answers through memory of its own that both see. */
	volatile int *ack = mmap(NULL, LEN, PROT_READ | PROT_WRITE,
				 MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	CHECK(ack != MAP_FAILED);

	int ro_fd = shm_open(name, O_RDONLY, 0);
	CHECK(ro_fd >= 0);
	volatile int *ro = mmap(NULL, LEN, PROT_READ, MAP_SHARED, ro_fd, 0);
	CHECK(ro != MAP_FAILED);
	expect_segv("file store", store_to, (void *)(ro + 4));

	fflush(stdout);
	pid_t pid = fork();
	if (pid == 0) {
		/* The child's view is its own, opened read-only. */
		int fd = shm_open(name, O_RDONLY, 0);
		volatile int *view = mmap(NULL, LEN, PROT_READ, MAP_SHARED, fd, 0);
		if (fd < 0 || view == MAP_FAILED)
			_exit(2);
		for (int i = 1; i <= ROUNDS; i++) {
			if (spin_until(view, i))
				_exit(3);
			__atomic_store_n(ack, i, __ATOMIC_RELEASE);
		}
		_exit(0);
	}
	CHECK(pid > 0);
	int ok = 1;
	for (int i = 1; i <= ROUNDS && ok; i++) {
		__atomic_store_n(rw, i, __ATOMIC_RELEASE);
		ok = spin_until(ack, i) == 0 && ro[0] == i;
	}
	int status;
	CHECK(waitpid(pid, &status, 0) == pid);
	CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
	CHECK(ok);
	if (ok && WIFEXITED(status) && WEXITSTATUS(status) == 0)
		printf("file ping-pong ok\n");
	CHECK(shm_unlink(name) == 0);
}

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	segments();
	files();
	printf(fails ? "FAIL\n" : "PASS\n");
	return fails != 0;
}
