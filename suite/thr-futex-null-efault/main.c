// Futexes below mmap_min_addr, where Linux maps nothing: every call that reads the word, and
// every shared one, whose key needs the page, fails with EFAULT (get_futex_key,
// futex_get_value_locked); a private FUTEX_WAKE is keyed by the address alone and wakes
// nobody. LTP's futex_supported_by_kernel() calls futex(NULL, FUTEX_WAIT, 0, NULL), which must
// fail rather than sleep.
//#ExpectedStdout: wait=EFAULT wait_private=EFAULT wait_4k=EFAULT wake=EFAULT wake_private=0 cmp_requeue=EFAULT
//#ExpectedExitCode: 0
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

/* <linux/futex.h> is not part of the sysroot; asm-generic values. */
#define FUTEX_WAIT 0
#define FUTEX_WAKE 1
#define FUTEX_CMP_REQUEUE 4
#define FUTEX_PRIVATE_FLAG 128

static int word;

static long futex(int *addr, int op, int val, long val2, int *addr2, int val3)
{
	return syscall(SYS_futex, addr, op, val, val2, addr2, val3);
}

static const char *err(long r)
{
	if (r >= 0) {
		static char buf[24];
		snprintf(buf, sizeof buf, "%ld", r);
		return buf;
	}
	switch (errno) {
	case EFAULT: return "EFAULT";
	case EAGAIN: return "EAGAIN";
	default: return strerror(errno);
	}
}

int main(void)
{
	/* A wait that sleeps instead of failing ends the test. */
	alarm(10);
	printf("wait=%s ", err(futex(NULL, FUTEX_WAIT, 0, 0, NULL, 0)));
	printf("wait_private=%s ", err(futex(NULL, FUTEX_WAIT | FUTEX_PRIVATE_FLAG, 0, 0, NULL, 0)));
	printf("wait_4k=%s ", err(futex((int *)4096, FUTEX_WAIT, 0, 0, NULL, 0)));
	printf("wake=%s ", err(futex(NULL, FUTEX_WAKE, 1, 0, NULL, 0)));
	printf("wake_private=%s ", err(futex(NULL, FUTEX_WAKE | FUTEX_PRIVATE_FLAG, 1, 0, NULL, 0)));
	printf("cmp_requeue=%s\n",
	       err(futex(NULL, FUTEX_CMP_REQUEUE | FUTEX_PRIVATE_FLAG, 1, 1, &word, 0)));
	return 0;
}
