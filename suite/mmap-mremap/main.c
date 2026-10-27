// mremap as mm/mremap.c has it: MREMAP_FIXED onto a range that overlaps the source fails with
// EINVAL and keeps the data, the parameters are checked before the source, MREMAP_DONTUNMAP moves
// the pages and leaves the source mapped (zeroes for anonymous memory, the file for a private
// file mapping), and an old length of 0 duplicates a shared file mapping but not a private one.
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef MREMAP_DONTUNMAP
#define MREMAP_DONTUNMAP 4
#endif

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: CHECK(%s) failed (errno %s)\n", __LINE__, #c, strerror(errno)); fails++; } } while (0)
#define FAILS(call, e) do { errno = 0; void *r_ = (call); if (r_ != MAP_FAILED || errno != (e)) { fprintf(stderr, "line %d: %s = %p (%s), want MAP_FAILED %s\n", __LINE__, #call, r_, strerror(errno), #e); fails++; } } while (0)

int main(void)
{
	long page = sysconf(_SC_PAGESIZE);
	char *src = mmap(NULL, 2 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	char *far = mmap(NULL, 3 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	CHECK(src != MAP_FAILED && far != MAP_FAILED);
	strcpy(src, "first");
	strcpy(src + page, "second");

	/* vrm_overlaps: from either side, and onto itself. */
	FAILS(mremap(src, 2 * page, 2 * page, MREMAP_MAYMOVE | MREMAP_FIXED, src + page), EINVAL);
	FAILS(mremap(src, 2 * page, 2 * page, MREMAP_MAYMOVE | MREMAP_FIXED, src), EINVAL);
	FAILS(mremap(src + page, page, 2 * page, MREMAP_MAYMOVE | MREMAP_FIXED, src), EINVAL);
	CHECK(strcmp(src, "first") == 0 && strcmp(src + page, "second") == 0);
	FAILS(mremap(src, page, page, MREMAP_FIXED, far), EINVAL);
	FAILS(mremap(src, page, page, MREMAP_MAYMOVE | MREMAP_FIXED, far + 1), EINVAL);
	FAILS(mremap(src, page, 2 * page, MREMAP_DONTUNMAP | MREMAP_MAYMOVE, NULL), EINVAL);
	FAILS(mremap(src, page, page, MREMAP_DONTUNMAP, NULL), EINVAL);
	FAILS(mremap(src, page, page, 8, NULL), EINVAL);

	char *moved = mremap(src, 2 * page, 2 * page, MREMAP_MAYMOVE | MREMAP_FIXED, far + page);
	CHECK(moved == far + page);
	CHECK(strcmp(moved, "first") == 0 && strcmp(moved + page, "second") == 0);

	/* MREMAP_DONTUNMAP: the pages move, the source stays mapped and reads as new memory. */
	char *anon = mmap(NULL, page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	strcpy(anon, "anonymous");
	char *away = mremap(anon, page, page, MREMAP_MAYMOVE | MREMAP_DONTUNMAP, NULL);
	CHECK(away != MAP_FAILED && away != anon);
	CHECK(away != MAP_FAILED && strcmp(away, "anonymous") == 0);
	CHECK(anon[0] == 0);
	CHECK(madvise(anon, page, MADV_DONTNEED) == 0);

	int fd = open("/tmp/mremap", O_CREAT | O_RDWR | O_TRUNC, 0600);
	CHECK(fd >= 0);
	CHECK(write(fd, "from the file", 13) == 13);
	char *priv = mmap(NULL, page, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
	CHECK(priv != MAP_FAILED);
	memcpy(priv, "FROM", 4);
	char *priv_away = mremap(priv, page, page, MREMAP_MAYMOVE | MREMAP_DONTUNMAP, NULL);
	CHECK(priv_away != MAP_FAILED && memcmp(priv_away, "FROM the file", 13) == 0);
	CHECK(memcmp(priv, "from the file", 13) == 0);

	/* An old length of 0: a second mapping of a shared mapping, never of a private one. */
	FAILS(mremap(priv, 0, page, MREMAP_MAYMOVE, NULL), EINVAL);
	char *shared = mmap(NULL, page, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	CHECK(shared != MAP_FAILED);
	FAILS(mremap(shared, 0, page, 0, NULL), ENOMEM);
	char *dup = mremap(shared, 0, page, MREMAP_MAYMOVE, NULL);
	CHECK(dup != MAP_FAILED && dup != shared);
	CHECK(dup != MAP_FAILED && memcmp(dup, "from the file", 13) == 0);
	if (dup != MAP_FAILED) {
		memcpy(dup, "FROM", 4);
		CHECK(msync(dup, page, MS_SYNC) == 0);
		char buf[13];
		CHECK(pread(fd, buf, 13, 0) == 13 && memcmp(buf, "FROM the file", 13) == 0);
	}
	close(fd);
	unlink("/tmp/mremap");

	printf(fails ? "FAIL\n" : "PASS\n");
	return fails != 0;
}
