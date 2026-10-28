// Filling the address space with PROT_NONE mappings, as libc-test's t_vmfill does before its
// out-of-memory tests: probing for the largest mapping with mmap and munmap, then mapping it,
// until nothing fits. The probes grow memory once and every later mapping reuses their pages,
// which must read zero without the host committing them: the fill takes no host memory, as on
// Linux (VmRSS in the guest is the size of memory, so measure the host's, or run the suite
// under a memory cap). The filled space is given back and filled again to the same size, and
// pages written before they were unmapped read zero when mapped again.
//#ExpectedStdout: fill ok
//#ExpectedStdout: reuse zeroed ok
//#ExpectedStdout: refill ok
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define MAX_MAPS 256

static void *maps[MAX_MAPS];
static size_t sizes[MAX_MAPS];
static size_t page;

/* The largest mapping that fits, at most `*start`, which becomes the largest power of 2 that
   did (libc-test's mmax). */
static size_t mmax(size_t *start)
{
	size_t i, n;
	for (i = n = *start; i >= page; i /= 2) {
		void *p = mmap(0, n, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (p == MAP_FAILED) {
			n -= i / 2;
		} else {
			munmap(p, n);
			if (n == i) *start = n;
			n += i / 2;
		}
	}
	return n & -page;
}

/* Fills the address space; returns the number of mappings and their total size in `*total`. */
static int fill(size_t *total)
{
	size_t start = SIZE_MAX / 2 + 1, m;
	int count = 0;
	*total = 0;
	while ((m = mmax(&start))) {
		void *q = mmap(0, m, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (q == MAP_FAILED || count == MAX_MAPS) {
			printf("FAIL mapping %d of %zu bytes\n", count, m);
			return -1;
		}
		maps[count] = q;
		sizes[count++] = m;
		*total += m;
	}
	return count;
}

static void unfill(int count)
{
	for (int i = 0; i < count; i++)
		if (munmap(maps[i], sizes[i])) printf("FAIL munmap %d\n", i);
}

static int all_zero(const unsigned char *p, size_t len)
{
	for (size_t i = 0; i < len; i++)
		if (p[i]) return 0;
	return 1;
}

int main(void)
{
	page = sysconf(_SC_PAGESIZE);
	size_t total, again;
	int count = fill(&total);
	/* Everything above the image is mapped: nothing fits any more, and the space is large. */
	if (count <= 0 || total < ((size_t)1 << 30) ||
	    mmap(0, page, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) != MAP_FAILED) {
		printf("FAIL fill: %d mappings, %zu bytes\n", count, total);
		return 1;
	}
	printf("fill ok\n");
	unfill(count);

	/* Write the start, the middle and the end of a large mapping, unmap it, and map the same
	   pages again, read-write: they read zero. */
	size_t len = (size_t)256 << 20;
	unsigned char *p = mmap(0, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED) {
		printf("FAIL mmap after unfill\n");
		return 1;
	}
	memset(p, 0x5a, page);
	memset(p + len / 2, 0x5a, page);
	memset(p + len - page, 0x5a, page);
	munmap(p, len);
	unsigned char *q = mmap(0, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (q == MAP_FAILED || !all_zero(q, page) || !all_zero(q + len / 2, page) ||
	    !all_zero(q + len - page, page)) {
		printf("FAIL reused pages do not read zero\n");
		return 1;
	}
	printf("reuse zeroed ok\n");
	munmap(q, len);

	count = fill(&again);
	if (count <= 0 || again != total) {
		printf("FAIL refill: %d mappings, %zu bytes, first fill %zu\n", count, again, total);
		return 1;
	}
	printf("refill ok\n");
	return 0;
}
