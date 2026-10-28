// fork of a process with a large, sparsely written heap: every child, and a grandchild, sees its
// parent's memory as it was at the fork, whichever pages the parent wrote, only read or never
// touched: a pattern on every 37th page of a 32 MiB heap, pages written with zeros, a data
// segment the parent changed or cleared, the stack, pages another thread wrote just before the
// fork, and an untouched mapping that reads zero. A null store still faults in a child.
//#BuildEnv: WASIXCC_WASM_EXCEPTIONS=no
//#ExpectedStdout: grandchild ok
//#ExpectedStdout: children=12 ok
//#ExpectedStdout: null store in child: signaled=1 sig=11
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#define PAGE 4096
#define HEAP (32 << 20)
#define FRESH (8 << 20)
#define THREAD_PAGES 64
#define CHILDREN 12

/* A data segment: the parent changes one half and clears the other. */
static unsigned char data[4 * PAGE] = {[0 ... 4 * PAGE - 1] = 0x5a};

static unsigned char *heap;
static unsigned char *fresh;
static unsigned char *by_thread;
static int generation;
static uint64_t expected;

static int go, done;

static uint64_t hash(const unsigned char *p, size_t n, uint64_t h)
{
	for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 0x100000001b3ull;
	return h;
}

static uint64_t hash_all(void)
{
	uint64_t h = 0xcbf29ce484222325ull;
	h = hash(heap, HEAP, h);
	h = hash(data, sizeof data, h);
	h = hash(by_thread, THREAD_PAGES * PAGE, h);
	return h;
}

/* Writes a page pattern of this generation: every 37th page, and zeros over every 41st. */
static void write_heap(void)
{
	for (size_t page = generation % 37; page < HEAP / PAGE; page += 37)
		memset(heap + page * PAGE + generation % 100, generation + page, 64);
	for (size_t page = generation % 41; page < HEAP / PAGE; page += 41)
		memset(heap + page * PAGE, 0, PAGE);
}

/* Another thread writes the pages it was asked to just before each fork. */
static void *writer(void *arg)
{
	(void)arg;
	for (int g = 1; g <= CHILDREN; g++) {
		while (__atomic_load_n(&go, __ATOMIC_ACQUIRE) < g) sched_yield();
		for (int page = 0; page < THREAD_PAGES; page += 1 + g % 3)
			by_thread[page * PAGE + g] = (unsigned char)(g * 7 + page);
		__atomic_store_n(&done, g, __ATOMIC_RELEASE);
	}
	return NULL;
}

static int fresh_is_zero(void)
{
	for (size_t i = 0; i < FRESH; i++)
		if (fresh[i]) return 0;
	return 1;
}

/* What a child checks: its memory equals the parent's at the fork, and its stack too. */
static int check(const unsigned char *stack, int stack_value)
{
	if (hash_all() != expected) {
		printf("generation %d: memory differs from the parent's\n", generation);
		return 0;
	}
	for (int i = 0; i < 2 * PAGE; i += 97)
		if (stack[i] != (unsigned char)(stack_value + i)) {
			printf("generation %d: stack differs at %d\n", generation, i);
			return 0;
		}
	if (!fresh_is_zero()) {
		printf("generation %d: an untouched mapping is not zero\n", generation);
		return 0;
	}
	return 1;
}

static int wait_ok(pid_t pid)
{
	int status;
	return pid > 0 && waitpid(pid, &status, 0) == pid && WIFEXITED(status) &&
	       WEXITSTATUS(status) == 0;
}

int main(void)
{
	heap = malloc(HEAP);
	by_thread = malloc(THREAD_PAGES * PAGE);
	fresh = mmap(NULL, FRESH, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (!heap || !by_thread || fresh == MAP_FAILED) {
		perror("alloc");
		return 1;
	}
	memset(by_thread, 0, THREAD_PAGES * PAGE);
	memset(data + 2 * PAGE, 0, 2 * PAGE);
	pthread_t thread;
	pthread_create(&thread, NULL, writer, NULL);

	unsigned char stack[2 * PAGE];
	int ok = 0;
	for (generation = 1; generation <= CHILDREN; generation++) {
		write_heap();
		data[generation * 13] = (unsigned char)generation;
		for (int i = 0; i < 2 * PAGE; i += 97) stack[i] = (unsigned char)(generation + i);
		__atomic_store_n(&go, generation, __ATOMIC_RELEASE);
		while (__atomic_load_n(&done, __ATOMIC_ACQUIRE) < generation) sched_yield();
		expected = hash_all();

		fflush(stdout);
		pid_t pid = fork();
		if (pid == 0) {
			if (!check(stack, generation)) _exit(1);
			if (generation == CHILDREN / 2) {
				/* The grandchild copies a copy. */
				write_heap();
				expected = hash_all();
				fflush(stdout);
				pid_t grandchild = fork();
				if (grandchild == 0) _exit(check(stack, generation) ? 0 : 1);
				if (!wait_ok(grandchild)) _exit(2);
				printf("grandchild ok\n");
				fflush(stdout);
			}
			_exit(0);
		}
		ok += wait_ok(pid);
		/* The child's changes stayed in the child. */
		if (hash_all() != expected) {
			printf("generation %d: the parent's memory changed\n", generation);
			return 1;
		}
	}
	pthread_join(thread, NULL);
	printf("children=%d %s\n", CHILDREN, ok == CHILDREN && fresh_is_zero() ? "ok" : "failed");

	fflush(stdout);
	pid_t pid = fork();
	if (pid == 0) {
		*(volatile int *)16 = 1;
		_exit(0);
	}
	int status = 0;
	waitpid(pid, &status, 0);
	printf("null store in child: signaled=%d sig=%d\n", WIFSIGNALED(status),
	       WIFSIGNALED(status) ? WTERMSIG(status) : 0);
	return 0;
}
