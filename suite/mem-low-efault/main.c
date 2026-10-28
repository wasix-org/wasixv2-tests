// Below 64 KiB is the guard at address 0: syscalls that write to a buffer there fail with
// EFAULT, as with an address the process cannot access on Linux, while empty buffers are never
// checked; fixed mappings there fail with EPERM, as below Linux's mmap_min_addr, which no
// capability lowers; and /proc/sys/vm/mmap_min_addr says so.
//
// The guard is read-only for now, so syscalls that only read a buffer there succeed, where
// Linux fails them with EFAULT: clang's wasm32 backend passes a null va_list buffer to variadic
// calls without variadic arguments, which musl reads anyway. The follow-up in wasmer's
// lib/wasixv2/DESIGN.md, "Guard page at 0", makes the guard inaccessible.
//#ExpectedStdout: ok
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

static int failures;
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
#define CHECK(cond)                                                                       \
	do {                                                                              \
		if (!(cond)) {                                                            \
			printf("FAIL line %d: %s (errno %d: %s)\n", __LINE__, #cond, errno, \
			       strerror(errno));                                          \
			failures++;                                                       \
		}                                                                         \
	} while (0)

static volatile uintptr_t low;

static void *at(uintptr_t addr)
{
	low = addr;
	return (void *)low;
}

int main(void)
{
	size_t ps = sysconf(_SC_PAGESIZE);
	int fds[2];
	CHECK(pipe(fds) == 0);
	/* Each read finds a byte waiting, so it reaches the buffer instead of blocking. */
#define FILL() CHECK(write(fds[1], "x", 1) == 1)

	FILL();
	CHECK_ERR(read(fds[0], at(0), 1), EFAULT);
	FILL();
	CHECK_ERR(read(fds[0], at(65535), 1), EFAULT);
	/* By design for now: the kernel reads the zeros there (EFAULT on Linux). */
	CHECK(write(fds[1], at(4096), 1) == 1);
	FILL();
	struct iovec iov = {at(8192), 1};
	CHECK_ERR(readv(fds[0], &iov, 1), EFAULT);
	CHECK_ERR(clock_gettime(CLOCK_MONOTONIC, at(16)), EFAULT);
	/* A buffer that only starts in the guard faults too. */
	FILL();
	CHECK_ERR(syscall(SYS_read, fds[0], at(65536 - 1), 2), EFAULT);
	/* Empty buffers are not checked. */
	CHECK(read(fds[0], at(0), 0) == 0);
	CHECK(write(fds[1], at(0), 0) == 0);

	int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED;
	CHECK(mmap(at(0), ps, PROT_READ | PROT_WRITE, flags, -1, 0) == MAP_FAILED &&
	      errno == EPERM);
	CHECK(mmap(at(65536 - ps), ps, PROT_READ | PROT_WRITE, flags, -1, 0) == MAP_FAILED &&
	      errno == EPERM);
	/* A hint below the minimum is only a hint. */
	void *p = mmap(at(4096), ps, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	CHECK(p != MAP_FAILED && (uintptr_t)p >= 65536);

	char text[32] = {0};
	int fd = open("/proc/sys/vm/mmap_min_addr", O_RDONLY);
	CHECK(fd >= 0);
	CHECK(read(fd, text, sizeof text - 1) > 0);
	CHECK(strcmp(text, "65536\n") == 0);
	close(fd);

	if (failures)
		return 1;
	puts("ok");
	return 0;
}
