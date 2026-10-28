// mlock, mlockall and RLIMIT_MEMLOCK (mm/mlock.c) for a process without CAP_IPC_LOCK: locked pages
// count against the soft limit, those already locked once (ENOMEM past it); a range running into
// an unmapped page locks the pages before it and fails with ENOMEM; mlockall(MCL_CURRENT) needs
// all of memory in use under the limit (ENOMEM, as LTP's mlockall03 expects); after
// mlockall(MCL_FUTURE) new mappings are locked, and one past the limit fails with EAGAIN; a
// limit of 0 forbids locking (EPERM) but not unlocking. /proc/self/status's VmLck counts the
// locked pages. Root holds CAP_IPC_LOCK, which lifts the limit.
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: CHECK(%s) failed (errno %s)\n", __LINE__, #c, strerror(errno)); fails++; } } while (0)
#define FAILS(call, e) do { errno = 0; long r_ = (long)(call); if (r_ != -1 || errno != (e)) { fprintf(stderr, "line %d: %s = %ld (%s), want -1 %s\n", __LINE__, #call, r_, strerror(errno), #e); fails++; } } while (0)

static long page;

/// VmLck of /proc/self/status, in pages; read without allocating, so nothing is mapped meanwhile.
static long locked_pages(void)
{
	char buf[4096];
	int fd = open("/proc/self/status", O_RDONLY);
	if (fd < 0)
		return -1;
	ssize_t n = read(fd, buf, sizeof buf - 1);
	close(fd);
	buf[n > 0 ? n : 0] = 0;
	char *line = strstr(buf, "VmLck:");
	return line ? strtol(line + 6, NULL, 10) * 1024 / page : -1;
}

static void set_limit(rlim_t bytes)
{
	struct rlimit limit;
	CHECK(getrlimit(RLIMIT_MEMLOCK, &limit) == 0);
	limit.rlim_cur = bytes;
	CHECK(setrlimit(RLIMIT_MEMLOCK, &limit) == 0);
}

static void *map(size_t len, int flags)
{
	return mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | flags, -1, 0);
}

static void unprivileged(void)
{
	char *p = map(4 * page, 0);
	CHECK(p != MAP_FAILED);
	CHECK(munmap(p + 3 * page, page) == 0);
	set_limit(2 * page);

	CHECK(mlock(p, 2 * page) == 0);
	CHECK(locked_pages() == 2);
	FAILS(mlock(p, 3 * page), ENOMEM);
	CHECK(mlock(p + page, 1) == 0);
	CHECK(locked_pages() == 2);
	CHECK(munlock(p + 100, 1) == 0);
	CHECK(locked_pages() == 1);
	CHECK(munlockall() == 0);
	CHECK(locked_pages() == 0);
	FAILS(mlock(p + 2 * page, 2 * page), ENOMEM);
	CHECK(locked_pages() == 1);
	CHECK(munlockall() == 0);
	CHECK(locked_pages() == 0);

	FAILS(mlockall(MCL_CURRENT), ENOMEM);
	FAILS(mlockall(MCL_ONFAULT), EINVAL);
	CHECK(mlockall(MCL_FUTURE) == 0);
	void *big = map(3 * page, 0);
	int big_errno = errno;
	void *one = map(page, 0);
	long future = locked_pages();
	CHECK(munlockall() == 0);
	CHECK(big == MAP_FAILED && big_errno == EAGAIN);
	CHECK(one != MAP_FAILED && future == 1);
	CHECK(locked_pages() == 0);

	set_limit(0);
	FAILS(mlock(p, page), EPERM);
	CHECK(munlock(p, page) == 0);
	FAILS(mlockall(MCL_CURRENT), EPERM);
	// The raw call: musl's mmap turns this EPERM into ENOMEM.
	int locked = MAP_PRIVATE | MAP_ANONYMOUS | MAP_LOCKED;
	FAILS(syscall(SYS_mmap, NULL, page, PROT_READ | PROT_WRITE, locked, -1, 0), EPERM);
}

static void privileged(void)
{
	char *p = map(2 * page, 0);
	CHECK(p != MAP_FAILED);
	set_limit(0);
	CHECK(mlock(p, 2 * page) == 0);
	CHECK(locked_pages() == 2);
	CHECK(mlockall(MCL_CURRENT) == 0);
	CHECK(locked_pages() > 2);
	CHECK(munlockall() == 0);
	CHECK(locked_pages() == 0);
}

int main(void)
{
	page = sysconf(_SC_PAGESIZE);
	if (geteuid() != 0)
		unprivileged();
	else
		privileged();
	if (fails)
		return 1;
	puts("PASS");
	return 0;
}
