// Shared file mappings: msync and munmap write stores back to the file, read() and write()
// of the same process agree with the mapping without an msync, bytes past the end of the file
// are never written back, two mappings of one file meet at msync, and the checks of
// MAP_SHARED and MAP_SHARED_VALIDATE.
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef MAP_SHARED_VALIDATE
#define MAP_SHARED_VALIDATE 0x03
#endif
#ifndef MAP_SYNC
#define MAP_SYNC 0x80000
#endif

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: CHECK(%s) failed (errno %s)\n", __LINE__, #c, strerror(errno)); fails++; } } while (0)
#define FAILS(call, e) do { errno = 0; long r_ = (long)(call); if (r_ != -1 || errno != (e)) { fprintf(stderr, "line %d: %s = %ld (%s), want -1 %s\n", __LINE__, #call, r_, strerror(errno), #e); fails++; } } while (0)

int main(void)
{
	long ps = sysconf(_SC_PAGESIZE);
	char buf[64];
	int fd = open("/tmp/shared", O_CREAT | O_RDWR | O_TRUNC, 0644);
	CHECK(fd >= 0);
	CHECK(write(fd, "0123456789", 10) == 10);

	char *p = mmap(NULL, ps, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	CHECK(p != MAP_FAILED);
	CHECK(memcmp(p, "0123456789", 10) == 0);

	/* msync writes the stores back; the size does not change. */
	memcpy(p + 2, "ab", 2);
	memcpy(p + 100, "past the end", 12);
	CHECK(msync(p, ps, MS_SYNC) == 0);
	CHECK(pread(fd, buf, sizeof buf, 0) == 10 && memcmp(buf, "01ab456789", 10) == 0);
	struct stat st;
	CHECK(fstat(fd, &st) == 0 && st.st_size == 10);
	/* What lies past the end stays in the mapping, as in the last page of the page cache. */
	CHECK(memcmp(p + 100, "past the end", 12) == 0);

	/* read() and write() of this process see the same bytes as the mapping. */
	p[0] = 'R';
	CHECK(pread(fd, buf, 1, 0) == 1 && buf[0] == 'R');
	CHECK(pwrite(fd, "W", 1, 9) == 1);
	CHECK(p[9] == 'W');
	CHECK(lseek(fd, 5, SEEK_SET) == 5 && write(fd, "w", 1) == 1);
	CHECK(p[5] == 'w');

	/* A second mapping of the file meets the first at msync. */
	char *q = mmap(NULL, ps, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	CHECK(q != MAP_FAILED && q[0] == 'R');
	q[1] = 'Q';
	CHECK(msync(q, ps, MS_ASYNC) == 0);
	CHECK(msync(p, ps, MS_INVALIDATE) == 0);
	CHECK(p[1] == 'Q');

	/* munmap writes back too. */
	p[3] = 'U';
	CHECK(munmap(p, ps) == 0);
	CHECK(pread(fd, buf, 4, 0) == 4 && memcmp(buf, "RQaU", 4) == 0);
	CHECK(munmap(q, ps) == 0);

	/* Growing the file makes more of the mapping reach it. */
	char *g = mmap(NULL, ps, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	CHECK(g != MAP_FAILED);
	CHECK(ftruncate(fd, 20) == 0);
	CHECK(g[15] == 0);
	g[15] = 'G';
	CHECK(msync(g, ps, MS_SYNC) == 0);
	CHECK(pread(fd, buf, 1, 15) == 1 && buf[0] == 'G');
	CHECK(munmap(g, ps) == 0);

	/* Writable shared mappings need a writable descriptor; read-only ones do not. */
	int rdonly = open("/tmp/shared", O_RDONLY);
	CHECK(rdonly >= 0);
	FAILS(mmap(NULL, ps, PROT_READ | PROT_WRITE, MAP_SHARED, rdonly, 0), EACCES);
	char *ro = mmap(NULL, ps, PROT_READ, MAP_SHARED, rdonly, 0);
	CHECK(ro != MAP_FAILED && ro[0] == 'R');
	CHECK(munmap(ro, ps) == 0);
	FAILS(mmap(NULL, ps, PROT_READ, MAP_SHARED_VALIDATE | MAP_SYNC, fd, 0), EOPNOTSUPP);
	char *v = mmap(NULL, ps, PROT_READ, MAP_SHARED_VALIDATE, fd, 0);
	CHECK(v != MAP_FAILED);
	FAILS(mmap(NULL, ps, PROT_READ, MAP_SHARED_VALIDATE | MAP_ANONYMOUS, -1, 0), EINVAL);
	FAILS(msync(v + 1, ps, MS_SYNC), EINVAL);
	FAILS(msync(v, ps, MS_SYNC | MS_ASYNC), EINVAL);

	printf(fails ? "FAIL\n" : "PASS\n");
	return fails != 0;
}
