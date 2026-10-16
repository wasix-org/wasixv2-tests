// Private file mappings: the pages hold the file, stores stay private, the tail past the end
// of the file reads as zeroes, offsets select pages, MADV_DONTNEED and mremap refill from the
// file, and mmap's errors for the file and its access mode.
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: CHECK(%s) failed (errno %s)\n", __LINE__, #c, strerror(errno)); fails++; } } while (0)
#define FAILS(call, e) do { errno = 0; long r_ = (long)(call); if (r_ != -1 || errno != (e)) { fprintf(stderr, "line %d: %s = %ld (%s), want -1 %s\n", __LINE__, #call, r_, strerror(errno), #e); fails++; } } while (0)

int main(void)
{
	long ps = sysconf(_SC_PAGESIZE);
	int fd = open("/tmp/private", O_CREAT | O_RDWR | O_TRUNC, 0644);
	CHECK(fd >= 0);
	/* Two pages and a bit: page 0 is 'a', page 1 is 'b', then "tail". */
	static char page[1 << 16];
	memset(page, 'a', ps);
	CHECK(write(fd, page, ps) == ps);
	memset(page, 'b', ps);
	CHECK(write(fd, page, ps) == ps);
	CHECK(write(fd, "tail", 4) == 4);

	char *p = mmap(NULL, 3 * ps, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
	CHECK(p != MAP_FAILED);
	CHECK(p[0] == 'a' && p[ps - 1] == 'a' && p[ps] == 'b');
	CHECK(memcmp(p + 2 * ps, "tail", 4) == 0);
	CHECK(p[2 * ps + 4] == 0 && p[3 * ps - 1] == 0);

	/* Stores stay in the mapping. */
	p[0] = 'X';
	char c;
	CHECK(pread(fd, &c, 1, 0) == 1 && c == 'a');
	/* MADV_DONTNEED drops the private copy; the file shows through again. */
	CHECK(madvise(p, ps, MADV_DONTNEED) == 0);
	CHECK(p[0] == 'a');

	/* An offset selects the pages. */
	char *q = mmap(NULL, ps, PROT_READ, MAP_PRIVATE, fd, ps);
	CHECK(q != MAP_FAILED && q[0] == 'b');
	FAILS(mmap(NULL, ps, PROT_READ, MAP_PRIVATE, fd, 1), EINVAL);

	/* Growing with mremap brings in more of the file. */
	char *r = mmap(NULL, ps, PROT_READ, MAP_PRIVATE, fd, 0);
	CHECK(r != MAP_FAILED);
	r = mremap(r, ps, 2 * ps, MREMAP_MAYMOVE);
	CHECK(r != MAP_FAILED && r[0] == 'a' && r[ps] == 'b');
	CHECK(munmap(r, 2 * ps) == 0);
	CHECK(munmap(q, ps) == 0);
	CHECK(munmap(p, 3 * ps) == 0);

	/* The file must be open for reading, be a regular file, and the fd a real one. */
	int wronly = open("/tmp/private", O_WRONLY);
	CHECK(wronly >= 0);
	FAILS(mmap(NULL, ps, PROT_READ, MAP_PRIVATE, wronly, 0), EACCES);
	int dir = open("/tmp", O_RDONLY | O_DIRECTORY);
	CHECK(dir >= 0);
	FAILS(mmap(NULL, ps, PROT_READ, MAP_PRIVATE, dir, 0), ENODEV);
	int pipefd[2];
	CHECK(pipe(pipefd) == 0);
	FAILS(mmap(NULL, ps, PROT_READ, MAP_PRIVATE, pipefd[0], 0), ENODEV);
	FAILS(mmap(NULL, ps, PROT_READ, MAP_PRIVATE, 99, 0), EBADF);
	/* A private mapping of a read-only descriptor may still be written. */
	int rdonly = open("/tmp/private", O_RDONLY);
	char *w = mmap(NULL, ps, PROT_READ | PROT_WRITE, MAP_PRIVATE, rdonly, 0);
	CHECK(w != MAP_FAILED);
	w[1] = 'Y';
	CHECK(pread(fd, &c, 1, 1) == 1 && c == 'a');
	/* The mapping outlives its descriptor. */
	close(rdonly);
	CHECK(w[0] == 'a' && w[1] == 'Y');

	printf(fails ? "FAIL\n" : "PASS\n");
	return fails != 0;
}
