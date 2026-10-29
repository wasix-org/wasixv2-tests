// mlockall(MCL_CURRENT) of a small program under the default RLIMIT_MEMLOCK of 8 MiB succeeds
// without CAP_IPC_LOCK (Open POSIX mlockall/8-1): it needs total_vm, the pages the process uses,
// under the limit (mm/mlock.c), and its stack counts as far as it was used, not as far as it may
// grow. The stack locked by MCL_CURRENT stays locked as it grows (acct_stack_growth): a megabyte
// deeper, VmLck is a megabyte larger, and it stays so once the stack is back.
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <unistd.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: CHECK(%s) failed (errno %s)\n", __LINE__, #c, strerror(errno)); fails++; } } while (0)

#define FRAME (64 << 10)

/// VmLck of /proc/self/status in KiB; read without allocating, so nothing is mapped meanwhile.
static long locked_kib(void)
{
	char buf[4096];
	int fd = open("/proc/self/status", O_RDONLY);
	if (fd < 0)
		return -1;
	ssize_t n = read(fd, buf, sizeof buf - 1);
	close(fd);
	buf[n > 0 ? n : 0] = 0;
	char *line = strstr(buf, "VmLck:");
	return line ? strtol(line + 6, NULL, 10) : -1;
}

/// Uses `depth` frames of 64 KiB more stack, and locks a byte of the deepest one.
static int deeper(int depth)
{
	volatile char frame[FRAME];
	memset((char *)frame, depth, sizeof frame);
	if (depth == 0)
		return mlock((char *)frame, 1);
	return deeper(depth - 1) + frame[FRAME / 2] - depth;
}

int main(void)
{
	struct rlimit limit;
	CHECK(getrlimit(RLIMIT_MEMLOCK, &limit) == 0);
	limit.rlim_cur = 8 << 20;
	if (limit.rlim_max != RLIM_INFINITY && limit.rlim_max < limit.rlim_cur)
		limit.rlim_cur = limit.rlim_max;
	CHECK(setrlimit(RLIMIT_MEMLOCK, &limit) == 0);

	CHECK(mlockall(MCL_CURRENT) == 0);
	long before = locked_kib();
	CHECK(before > 0 && before < 8192);

	CHECK(deeper(16) == 0);
	long after = locked_kib();
	CHECK(after >= before + 960);
	CHECK(locked_kib() == after);

	CHECK(munlockall() == 0);
	CHECK(locked_kib() == 0);
	if (fails) {
		fprintf(stderr, "VmLck %ld kB, then %ld kB\n", before, after);
		return 1;
	}
	puts("PASS");
	return 0;
}
