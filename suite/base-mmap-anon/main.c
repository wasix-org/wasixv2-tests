// Anonymous mmap: zeroed and writable pages, reuse after munmap, MAP_FIXED and
// MAP_FIXED_NOREPLACE over pages mmap handed out, mremap, madvise(MADV_DONTNEED), mincore,
// msync, mlock, and the argument errors of each.
//#ExpectedStdout: ok
#define _GNU_SOURCE
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
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
#define CHECK_ERR(expr, err)                                                              \
	do {                                                                              \
		errno = 0;                                                                \
		long r_ = (long)(expr);                                                   \
		if (r_ != -1 || errno != (err)) {                                         \
			printf("FAIL line %d: %s = %ld errno %d, want %s\n", __LINE__, #expr, \
			       r_, errno, #err);                                          \
			failures++;                                                       \
		}                                                                         \
	} while (0)

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

static size_t ps;

static char *map(void *addr, size_t len, int flags)
{
	return mmap(addr, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | flags, -1, 0);
}

static int all(const char *p, size_t len, char c)
{
	for (size_t i = 0; i < len; i++)
		if (p[i] != c)
			return 0;
	return 1;
}

int main(void)
{
	ps = sysconf(_SC_PAGESIZE);
	CHECK(ps > 0 && (ps & (ps - 1)) == 0);

	/* Fresh mappings are page aligned, zeroed and writable. */
	char *p = map(NULL, 4 * ps, 0);
	CHECK(p != MAP_FAILED && (uintptr_t)p % ps == 0);
	CHECK(all(p, 4 * ps, 0));
	memset(p, 'a', 4 * ps);
	CHECK(all(p, 4 * ps, 'a'));

	/* A length that is not a page multiple covers whole pages. */
	char *odd = map(NULL, ps + 1, 0);
	CHECK(odd != MAP_FAILED);
	memset(odd, 'o', 2 * ps);
	CHECK(munmap(odd, ps + 1) == 0);

	/* Unmapped pages come back zeroed. */
	for (int i = 0; i < 8; i++) {
		char *q = map(NULL, 2 * ps, 0);
		CHECK(q != MAP_FAILED && all(q, 2 * ps, 0));
		memset(q, 'q', 2 * ps);
		CHECK(munmap(q, 2 * ps) == 0);
	}

	/* MAP_FIXED replaces part of an existing mapping with zeroed pages. */
	char *f = map(p + ps, ps, MAP_FIXED);
	CHECK(f == p + ps);
	CHECK(all(p, ps, 'a') && all(p + ps, ps, 0) && all(p + 2 * ps, 2 * ps, 'a'));

	/* MAP_FIXED_NOREPLACE refuses mapped pages and takes unmapped ones. */
	CHECK(map(p, ps, MAP_FIXED_NOREPLACE) == MAP_FAILED && errno == EEXIST);
	CHECK(munmap(p + 2 * ps, ps) == 0);
	f = map(p + 2 * ps, ps, MAP_FIXED_NOREPLACE);
	CHECK(f == p + 2 * ps && all(f, ps, 0));

	/* madvise. MADV_DONTNEED drops private anonymous pages, which then read as zero. */
	memset(p, 'd', 4 * ps);
	CHECK(madvise(p, ps, MADV_DONTNEED) == 0);
	CHECK(all(p, ps, 0) && all(p + ps, 3 * ps, 'd'));
	CHECK(madvise(p, 4 * ps, MADV_NORMAL) == 0);
	CHECK(madvise(p, 4 * ps, MADV_RANDOM) == 0);
	CHECK(madvise(p, 4 * ps, MADV_SEQUENTIAL) == 0);
	CHECK(madvise(p, 4 * ps, MADV_WILLNEED) == 0);
	CHECK(madvise(p, 4 * ps, MADV_FREE) == 0);
	CHECK(madvise(p, 0, MADV_NORMAL) == 0);
	CHECK_ERR(madvise(p + 1, ps, MADV_NORMAL), EINVAL);
	CHECK_ERR(madvise(p, ps, 12345), EINVAL);
	CHECK(posix_madvise(p, ps, POSIX_MADV_WILLNEED) == 0);

	/* mincore reports touched pages resident, and unmapped ranges are ENOMEM. */
	memset(p, 'm', 4 * ps);
	unsigned char vec[4] = {0};
	CHECK(mincore(p, 4 * ps, vec) == 0);
	CHECK((vec[0] & 1) && (vec[1] & 1) && (vec[2] & 1) && (vec[3] & 1));
	CHECK_ERR(mincore(p + 1, ps, vec), EINVAL);

	/* msync. */
	CHECK(msync(p, 4 * ps, MS_SYNC) == 0);
	CHECK(msync(p, 4 * ps, MS_ASYNC) == 0);
	CHECK(msync(p, 4 * ps, MS_INVALIDATE) == 0);
	CHECK_ERR(msync(p, ps, MS_SYNC | MS_ASYNC), EINVAL);
	CHECK_ERR(msync(p + 1, ps, MS_SYNC), EINVAL);
	CHECK_ERR(msync(p, ps, 0x100), EINVAL);

	/* mlock and munlock on a small range. */
	CHECK(mlock(p, ps) == 0);
	CHECK(munlock(p, ps) == 0);

	/* mremap: shrink in place, refuse to grow over a mapped page without MREMAP_MAYMOVE,
	 * grow by moving with it, keeping the contents. */
	memset(p, 'r', 4 * ps);
	char *r = mremap(p, 4 * ps, 3 * ps, 0);
	CHECK(r == p && all(r, 3 * ps, 'r'));
	char *g = map(NULL, 3 * ps, 0);
	CHECK(g != MAP_FAILED);
	memset(g, 'g', 3 * ps);
	CHECK(mremap(g, 2 * ps, 3 * ps, 0) == MAP_FAILED && errno == ENOMEM);
	char *moved = mremap(g, 3 * ps, 64 * ps, MREMAP_MAYMOVE);
	CHECK(moved != MAP_FAILED && all(moved, 3 * ps, 'g'));
	if (moved != MAP_FAILED) {
		memset(moved, 'x', 64 * ps);
		CHECK(munmap(moved, 64 * ps) == 0);
	}
	CHECK(mremap(r + 1, ps, 2 * ps, MREMAP_MAYMOVE) == MAP_FAILED && errno == EINVAL);
	CHECK(mremap(r, ps, 0, MREMAP_MAYMOVE) == MAP_FAILED && errno == EINVAL);
	CHECK(mremap(r, ps, 2 * ps, MREMAP_FIXED) == MAP_FAILED && errno == EINVAL);
	CHECK(mremap(r, ps, 2 * ps, 0x80) == MAP_FAILED && errno == EINVAL);
	CHECK(munmap(r, 3 * ps) == 0);

	/* Unmapped ranges: mincore and msync say ENOMEM. */
	CHECK_ERR(mincore(r, ps, vec), ENOMEM);
	CHECK_ERR(msync(r, ps, MS_SYNC), ENOMEM);

	/* Shared anonymous memory works within the process. */
	char *s = mmap(NULL, ps, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	CHECK(s != MAP_FAILED && all(s, ps, 0));
	if (s != MAP_FAILED) {
		s[0] = 1;
		CHECK(munmap(s, ps) == 0);
	}

	/* Flags that only hint are accepted. */
	char *h = map(NULL, ps, MAP_NORESERVE | MAP_POPULATE | MAP_STACK);
	CHECK(h != MAP_FAILED);
	CHECK(munmap(h, ps) == 0);

	/* Argument errors. */
	CHECK(map(NULL, 0, 0) == MAP_FAILED && errno == EINVAL);
	CHECK(mmap(NULL, ps, PROT_READ, MAP_ANONYMOUS, -1, 0) == MAP_FAILED && errno == EINVAL);
	CHECK(mmap(NULL, ps, PROT_READ, MAP_PRIVATE, -1, 0) == MAP_FAILED && errno == EBADF);
	CHECK(mmap(NULL, ps, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 1) == MAP_FAILED &&
	      errno == EINVAL);
	CHECK_ERR(munmap(p + 1, ps), EINVAL);
	CHECK_ERR(munmap(p, 0), EINVAL);
	if (sizeof(void *) == 4)
		CHECK(map(NULL, 0xfff00000u, 0) == MAP_FAILED && errno == ENOMEM);

	if (failures)
		return 1;
	puts("ok");
	return 0;
}
