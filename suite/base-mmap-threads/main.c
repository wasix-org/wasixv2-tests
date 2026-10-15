// Many threads, released together, mapping, growing and unmapping memory at once while malloc
// moves the break, as a multi-threaded runtime does when its workers map their stacks: every
// mapping is zeroed, no two live mappings overlap, and none of it fails.
//#ExpectedStdout: ok
#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define THREADS 16
#define ROUNDS 200
#define LIVE 16

static size_t ps;
static int failures;
static pthread_barrier_t start;

static void fail(int id, const char *what)
{
	printf("FAIL thread %d: %s\n", id, what);
	__atomic_fetch_add(&failures, 1, __ATOMIC_SEQ_CST);
}

static int all(const unsigned char *p, size_t len, unsigned char c)
{
	for (size_t i = 0; i < len; i += 256)
		if (p[i] != c)
			return 0;
	return 1;
}

static void *run(void *arg)
{
	int id = (int)(intptr_t)arg;
	unsigned char mark = (unsigned char)(id + 1);
	unsigned char *live[LIVE] = {0};
	size_t lens[LIVE] = {0};
	pthread_barrier_wait(&start);
	for (int round = 0; round < ROUNDS; round++) {
		int slot = round % LIVE;
		if (live[slot]) {
			// A mapping handed out twice shows another thread's mark.
			if (!all(live[slot], lens[slot], mark))
				fail(id, "mapping overwritten");
			if (munmap(live[slot], lens[slot]))
				fail(id, "munmap");
			live[slot] = 0;
		}
		size_t len = ps * (1 + (id + round) % 3);
		unsigned char *p =
			mmap(0, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (p == MAP_FAILED) {
			fail(id, "mmap");
			continue;
		}
		if (!all(p, len, 0))
			fail(id, "mapping not zeroed");
		memset(p, mark, len);
		if (round % 5 == 0) {
			unsigned char *q = mremap(p, len, len + ps, MREMAP_MAYMOVE);
			if (q == MAP_FAILED) {
				fail(id, "mremap");
			} else {
				if (!all(q, len, mark))
					fail(id, "mremap lost contents");
				memset(q + len, mark, ps);
				p = q;
				len += ps;
			}
		}
		live[slot] = p;
		lens[slot] = len;
		if (round % 4 == id % 4) {
			// Large enough for the allocator to move the break or map.
			unsigned char *m = calloc(1, 2 * ps);
			if (!m || !all(m, 2 * ps, 0))
				fail(id, "calloc");
			free(m);
		}
		sched_yield();
	}
	for (int slot = 0; slot < LIVE; slot++) {
		if (!live[slot])
			continue;
		if (!all(live[slot], lens[slot], mark))
			fail(id, "mapping overwritten");
		munmap(live[slot], lens[slot]);
	}
	return 0;
}

int main(void)
{
	ps = (size_t)sysconf(_SC_PAGESIZE);
	pthread_t t[THREADS];
	pthread_barrier_init(&start, 0, THREADS);
	for (int i = 0; i < THREADS; i++)
		if (pthread_create(&t[i], 0, run, (void *)(intptr_t)i))
			return 1;
	for (int i = 0; i < THREADS; i++)
		if (pthread_join(t[i], 0))
			return 2;
	if (failures)
		return 3;
	puts("ok");
	return 0;
}
