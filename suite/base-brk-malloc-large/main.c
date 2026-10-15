// The program break (brk grows, shrinks and hands out zeroed memory) and malloc on top of it:
// small and large blocks, realloc growth, calloc, and memory reused after free.
//#ExpectedStdout: ok
#define _GNU_SOURCE
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

static int failures;
#define CHECK(cond)                                                                       \
	do {                                                                              \
		if (!(cond)) {                                                            \
			printf("FAIL line %d: %s (errno %d: %s)\n", __LINE__, #cond, errno, \
			       strerror(errno));                                          \
			failures++;                                                       \
		}                                                                         \
	} while (0)

static int all(const unsigned char *p, size_t len, unsigned char c)
{
	for (size_t i = 0; i < len; i++)
		if (p[i] != c)
			return 0;
	return 1;
}

/* Runs before anything calls malloc, so moving the break cannot disturb the allocator. */
static void test_brk(void)
{
	size_t ps = sysconf(_SC_PAGESIZE);
	uintptr_t start = syscall(SYS_brk, 0);
	CHECK(start != 0);
	CHECK((uintptr_t)sbrk(0) == start);

	uintptr_t want = start + 3 * ps + 100;
	CHECK((uintptr_t)syscall(SYS_brk, want) == want);
	CHECK((uintptr_t)syscall(SYS_brk, 0) == want);
	CHECK(all((unsigned char *)start, want - start, 0));
	memset((void *)start, 0xab, want - start);

	/* Shrinking gives the pages back; growing again hands out zeroed memory. */
	CHECK((uintptr_t)syscall(SYS_brk, start) == start);
	CHECK((uintptr_t)syscall(SYS_brk, start + ps) == start + ps);
	/* Only whole pages above the break are dropped, so check from the first one. */
	uintptr_t page_end = (start + ps - 1) / ps * ps;
	CHECK(all((unsigned char *)page_end, start + ps - page_end, 0));

	/* The break never moves below where it started. */
	CHECK((uintptr_t)syscall(SYS_brk, start - ps) == start + ps);
	CHECK((uintptr_t)syscall(SYS_brk, start) == start);
}

int main(void)
{
	test_brk();

	for (size_t size = 1; size <= (64u << 20); size *= 2) {
		unsigned char *p = malloc(size);
		CHECK(p != NULL);
		if (!p)
			continue;
		memset(p, (unsigned char)size, size);
		CHECK(p[0] == (unsigned char)size && p[size - 1] == (unsigned char)size);
		free(p);
	}

	/* Large blocks are returned on free, so repeated allocations do not run out. */
	for (int i = 0; i < 64; i++) {
		unsigned char *p = malloc(64u << 20);
		CHECK(p != NULL);
		if (!p)
			break;
		p[0] = 1;
		p[(64u << 20) - 1] = 2;
		free(p);
	}

	unsigned char *big = malloc(256u << 20);
	CHECK(big != NULL);
	if (big) {
		memset(big, 0x5a, 256u << 20);
		CHECK(all(big + (128u << 20), 4096, 0x5a));
		free(big);
	}

	unsigned char *z = calloc(32u << 20, 1);
	CHECK(z != NULL && all(z, 32u << 20, 0));
	free(z);

	/* realloc keeps the contents while the block grows. */
	unsigned char *r = NULL;
	size_t len = 0;
	for (size_t size = 16; size <= (32u << 20); size *= 4) {
		unsigned char *n = realloc(r, size);
		CHECK(n != NULL);
		if (!n)
			break;
		CHECK(all(n, len, 0x77));
		memset(n + len, 0x77, size - len);
		r = n;
		len = size;
	}
	free(r);

	/* Many small blocks. */
	enum { N = 100000 };
	static void *blocks[N];
	for (int i = 0; i < N; i++) {
		blocks[i] = malloc(16 + i % 200);
		CHECK(blocks[i] != NULL);
		if (blocks[i])
			memset(blocks[i], i, 16);
	}
	for (int i = 0; i < N; i++)
		free(blocks[i]);

	errno = 0;
	CHECK(malloc(SIZE_MAX / 2) == NULL && errno == ENOMEM);

	if (failures)
		return 1;
	puts("ok");
	return 0;
}
