// Resource limits: getrlimit/setrlimit/prlimit semantics for an unprivileged process, the
// RLIMIT_STACK report, and RLIMIT_NOFILE bounding the descriptors open and dup allocate.
//#ExpectedStdout: ok
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
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

#define LIMIT 16

int main(void)
{
	struct rlimit nofile;
	CHECK(getrlimit(RLIMIT_NOFILE, &nofile) == 0);
	CHECK(nofile.rlim_cur >= 64 && nofile.rlim_cur <= nofile.rlim_max);
	CHECK(sysconf(_SC_OPEN_MAX) == (long)nofile.rlim_cur);

	struct rlimit stack;
	CHECK(getrlimit(RLIMIT_STACK, &stack) == 0);
	CHECK(stack.rlim_cur > 0 && stack.rlim_cur <= stack.rlim_max);

	int resources[] = {RLIMIT_CPU,    RLIMIT_FSIZE,   RLIMIT_DATA,	 RLIMIT_CORE,
			   RLIMIT_RSS,    RLIMIT_NPROC,   RLIMIT_MEMLOCK, RLIMIT_AS,
			   RLIMIT_LOCKS,  RLIMIT_SIGPENDING, RLIMIT_MSGQUEUE, RLIMIT_NICE,
			   RLIMIT_RTPRIO, RLIMIT_RTTIME};
	for (size_t i = 0; i < sizeof resources / sizeof resources[0]; i++) {
		struct rlimit rl;
		if (getrlimit(resources[i], &rl) != 0 || rl.rlim_cur > rl.rlim_max)
			printf("FAIL getrlimit(%d)\n", resources[i]), failures++;
	}
	struct rlimit rl;
	CHECK_ERR(getrlimit(99, &rl), EINVAL);

	/* prlimit on ourselves, by 0 and by pid; other pids do not exist. */
	struct rlimit got;
	CHECK(prlimit(0, RLIMIT_NOFILE, NULL, &got) == 0 && got.rlim_cur == nofile.rlim_cur);
	CHECK(prlimit(getpid(), RLIMIT_NOFILE, NULL, &got) == 0 &&
	      got.rlim_max == nofile.rlim_max);
	CHECK_ERR(prlimit(99999, RLIMIT_NOFILE, NULL, &got), ESRCH);

	/* The soft limit may not exceed the hard one, and the hard one may not grow. */
	rl = (struct rlimit){nofile.rlim_max, nofile.rlim_max};
	if (nofile.rlim_max != RLIM_INFINITY) {
		rl.rlim_cur = nofile.rlim_max + 1;
		CHECK_ERR(setrlimit(RLIMIT_NOFILE, &rl), EINVAL);
		rl.rlim_max = nofile.rlim_max + 1;
		CHECK_ERR(setrlimit(RLIMIT_NOFILE, &rl), EPERM);
	}

	/* A lower soft limit bounds new descriptors; existing ones stay. */
	rl = (struct rlimit){LIMIT, nofile.rlim_max};
	struct rlimit old;
	CHECK(prlimit(0, RLIMIT_NOFILE, &rl, &old) == 0 && old.rlim_cur == nofile.rlim_cur);
	CHECK(getrlimit(RLIMIT_NOFILE, &got) == 0 && got.rlim_cur == LIMIT);
	CHECK(sysconf(_SC_OPEN_MAX) == LIMIT);
	int last = -1;
	for (int i = 0; i < 2 * LIMIT; i++) {
		int fd = dup(0);
		if (fd < 0) {
			CHECK(errno == EMFILE);
			break;
		}
		last = fd;
	}
	CHECK(last == LIMIT - 1);
	CHECK_ERR(open("/", O_RDONLY | O_DIRECTORY), EMFILE);
	CHECK(close(LIMIT - 1) == 0);
	CHECK(dup(0) == LIMIT - 1);
	for (int fd = 3; fd < LIMIT; fd++)
		close(fd);

	/* Raising the soft limit back up to the hard one is always allowed. */
	CHECK(setrlimit(RLIMIT_NOFILE, &nofile) == 0);
	CHECK(getrlimit(RLIMIT_NOFILE, &got) == 0 && got.rlim_cur == nofile.rlim_cur);
	for (int i = 0; i < 2 * LIMIT; i++)
		CHECK(dup(0) >= 0);

	/* Lowering the hard limit is allowed, and is permanent. */
	rl = (struct rlimit){RLIM_INFINITY, RLIM_INFINITY};
	CHECK(getrlimit(RLIMIT_CORE, &rl) == 0);
	rl.rlim_cur = rl.rlim_max = 0;
	CHECK(setrlimit(RLIMIT_CORE, &rl) == 0);
	rl.rlim_max = 1;
	CHECK_ERR(setrlimit(RLIMIT_CORE, &rl), EPERM);

	if (failures)
		return 1;
	puts("ok");
	return 0;
}
