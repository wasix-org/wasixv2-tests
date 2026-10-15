// Scheduler basics for an unprivileged process: sched_yield, CPU affinity and getcpu, policy
// and parameter queries, priority ranges, nice values, and membarrier.
//#ExpectedStdout: ok
#define _GNU_SOURCE
#include <errno.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <time.h>
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

#define NO_SUCH_PID 99999

/* From linux/membarrier.h. */
#define MEMBARRIER_CMD_QUERY 0
#define MEMBARRIER_CMD_PRIVATE_EXPEDITED (1 << 3)
#define MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED (1 << 4)

int main(void)
{
	CHECK(sched_yield() == 0);

	cpu_set_t set;
	CPU_ZERO(&set);
	CHECK(sched_getaffinity(0, sizeof set, &set) == 0);
	int cpus = CPU_COUNT(&set);
	CHECK(cpus >= 1);
	CHECK(sched_setaffinity(0, sizeof set, &set) == 0);
	cpu_set_t empty;
	CPU_ZERO(&empty);
	CHECK_ERR(sched_setaffinity(0, sizeof empty, &empty), EINVAL);
	CHECK_ERR(sched_getaffinity(NO_SUCH_PID, sizeof set, &set), ESRCH);
	CHECK_ERR(syscall(SYS_sched_getaffinity, 0, 3, &set), EINVAL);

	int cpu = sched_getcpu();
	CHECK(cpu >= 0 && cpu < CPU_SETSIZE && CPU_ISSET(cpu, &set));
	unsigned c = 12345, node = 12345;
	CHECK(syscall(SYS_getcpu, &c, &node, NULL) == 0 && c == (unsigned)cpu && node == 0);

	/* musl leaves the policy calls to the kernel only through syscall(). */
	CHECK(syscall(SYS_sched_getscheduler, 0) == SCHED_OTHER);
	CHECK_ERR(syscall(SYS_sched_getscheduler, NO_SUCH_PID), ESRCH);
	CHECK_ERR(syscall(SYS_sched_getscheduler, -1), EINVAL);
	struct sched_param param = {.sched_priority = 7};
	CHECK(syscall(SYS_sched_getparam, 0, &param) == 0 && param.sched_priority == 0);
	CHECK(syscall(SYS_sched_setparam, 0, &param) == 0);
	param.sched_priority = 1;
	CHECK_ERR(syscall(SYS_sched_setparam, 0, &param), EINVAL);
	CHECK_ERR(syscall(SYS_sched_setscheduler, 0, SCHED_FIFO, &param), EPERM);
	param.sched_priority = 0;
	CHECK(syscall(SYS_sched_setscheduler, 0, SCHED_OTHER, &param) == 0);
	CHECK(syscall(SYS_sched_setscheduler, 0, SCHED_BATCH, &param) == 0);
	CHECK(syscall(SYS_sched_getscheduler, 0) == SCHED_BATCH);
	CHECK(syscall(SYS_sched_setscheduler, 0, SCHED_OTHER, &param) == 0);
	CHECK_ERR(syscall(SYS_sched_setscheduler, 0, 99, &param), EINVAL);

	CHECK(sched_get_priority_max(SCHED_OTHER) == 0 && sched_get_priority_min(SCHED_OTHER) == 0);
	CHECK(sched_get_priority_max(SCHED_FIFO) == 99 && sched_get_priority_min(SCHED_FIFO) == 1);
	CHECK(sched_get_priority_max(SCHED_RR) == 99 && sched_get_priority_min(SCHED_RR) == 1);
	CHECK_ERR(sched_get_priority_max(99), EINVAL);
	struct timespec ts;
	CHECK(sched_rr_get_interval(0, &ts) == 0 && ts.tv_sec >= 0);

	/* Nice values: raising is allowed, lowering again needs privilege. */
	errno = 0;
	int base = getpriority(PRIO_PROCESS, 0);
	CHECK(errno == 0 && base >= -20 && base <= 13);
#ifdef __wasixv2__
	CHECK(base == 0);
#endif
	CHECK(setpriority(PRIO_PROCESS, 0, base + 5) == 0);
	CHECK(getpriority(PRIO_PROCESS, 0) == base + 5);
	CHECK(nice(1) == base + 6);
	CHECK_ERR(setpriority(PRIO_PROCESS, 0, base), EACCES);
	CHECK(getpriority(PRIO_PROCESS, getpid()) == base + 6);
	CHECK_ERR(getpriority(PRIO_PROCESS, NO_SUCH_PID), ESRCH);
	CHECK_ERR(getpriority(99, 0), EINVAL);
	CHECK(setpriority(PRIO_PROCESS, 0, 100) == 0 && getpriority(PRIO_PROCESS, 0) == 19);

	/* membarrier: private expedited works once registered. */
	long cmds = syscall(SYS_membarrier, MEMBARRIER_CMD_QUERY, 0, 0);
	CHECK(cmds > 0 && (cmds & MEMBARRIER_CMD_PRIVATE_EXPEDITED));
	CHECK_ERR(syscall(SYS_membarrier, MEMBARRIER_CMD_PRIVATE_EXPEDITED, 0, 0), EPERM);
	CHECK(syscall(SYS_membarrier, MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED, 0, 0) == 0);
	CHECK(syscall(SYS_membarrier, MEMBARRIER_CMD_PRIVATE_EXPEDITED, 0, 0) == 0);
	CHECK_ERR(syscall(SYS_membarrier, 1 << 20, 0, 0), EINVAL);
	CHECK_ERR(syscall(SYS_membarrier, MEMBARRIER_CMD_QUERY, 1, 0), EINVAL);

	if (failures)
		return 1;
	puts("ok");
	return 0;
}
