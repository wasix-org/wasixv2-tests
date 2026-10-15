// mprotect accepts every protection except PROT_EXEC, and mmap refuses executable mappings:
// wasm cannot run code generated at run time, so wasixv2 fails them with EACCES, as a Linux
// system that forbids executable memory does. Protections are not enforced.
//#ExpectedStdout: ok
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
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

int main(void)
{
	size_t ps = sysconf(_SC_PAGESIZE);
	char *p = mmap(NULL, 2 * ps, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	CHECK(p != MAP_FAILED);

	CHECK(mprotect(p, 2 * ps, PROT_READ) == 0);
	CHECK(mprotect(p, ps, PROT_NONE) == 0);
	CHECK(mprotect(p, 2 * ps, PROT_READ | PROT_WRITE) == 0);
	p[0] = 'x';
	CHECK(p[0] == 'x');
	CHECK(mprotect(p, 0, PROT_READ) == 0);

	CHECK_ERR(mprotect(p, ps, PROT_EXEC), EACCES);
	CHECK_ERR(mprotect(p, ps, PROT_READ | PROT_EXEC), EACCES);
	CHECK_ERR(mprotect(p, ps, PROT_READ | PROT_WRITE | PROT_EXEC), EACCES);
	CHECK(mmap(NULL, ps, PROT_READ | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) ==
		      MAP_FAILED &&
	      errno == EACCES);
	CHECK(mmap(NULL, ps, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1,
		   0) == MAP_FAILED &&
	      errno == EACCES);
	/* The refused calls changed nothing. */
	p[ps] = 'y';
	CHECK(p[0] == 'x' && p[ps] == 'y');

	CHECK_ERR(syscall(SYS_mprotect, p + 1, ps, PROT_READ), EINVAL);
	CHECK_ERR(mprotect(p, ps, 0x10), EINVAL);
	CHECK(munmap(p + ps, ps) == 0);
	CHECK_ERR(mprotect(p, 2 * ps, PROT_READ), ENOMEM);

	if (failures)
		return 1;
	puts("ok");
	return 0;
}
