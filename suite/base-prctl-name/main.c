// prctl basics: the task name (PR_SET_NAME/PR_GET_NAME and pthread_setname_np), dumpable,
// parent-death signal, no_new_privs, timer slack, child subreaper, keepcaps and the bounding
// set, plus unknown options.
//#ExpectedStdout: ok
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/prctl.h>

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

int main(void)
{
	char name[32];
	memset(name, 'x', sizeof name);
	CHECK(prctl(PR_GET_NAME, name, 0, 0, 0) == 0);
	CHECK(memchr(name, 0, 16) != NULL && name[0] != 0);

	CHECK(prctl(PR_SET_NAME, "worker", 0, 0, 0) == 0);
	memset(name, 'x', sizeof name);
	CHECK(prctl(PR_GET_NAME, name, 0, 0, 0) == 0 && strcmp(name, "worker") == 0);

	/* Names are cut to 15 bytes. */
	CHECK(prctl(PR_SET_NAME, "abcdefghijklmnopqrstuvwxyz", 0, 0, 0) == 0);
	CHECK(prctl(PR_GET_NAME, name, 0, 0, 0) == 0 && strcmp(name, "abcdefghijklmno") == 0);

	CHECK(pthread_setname_np(pthread_self(), "pthread-name") == 0);
	CHECK(pthread_getname_np(pthread_self(), name, sizeof name) == 0 &&
	      strcmp(name, "pthread-name") == 0);
	CHECK(pthread_setname_np(pthread_self(), "this-name-is-too-long") == ERANGE);

	/* Dumpable. */
	CHECK(prctl(PR_GET_DUMPABLE, 0, 0, 0, 0) == 1);
	CHECK(prctl(PR_SET_DUMPABLE, 0, 0, 0, 0) == 0);
	CHECK(prctl(PR_GET_DUMPABLE, 0, 0, 0, 0) == 0);
	CHECK_ERR(prctl(PR_SET_DUMPABLE, 2, 0, 0, 0), EINVAL);
	CHECK(prctl(PR_SET_DUMPABLE, 1, 0, 0, 0) == 0);

	/* Parent-death signal. */
	int sig = -1;
	CHECK(prctl(PR_GET_PDEATHSIG, &sig, 0, 0, 0) == 0 && sig == 0);
	CHECK(prctl(PR_SET_PDEATHSIG, SIGTERM, 0, 0, 0) == 0);
	CHECK(prctl(PR_GET_PDEATHSIG, &sig, 0, 0, 0) == 0 && sig == SIGTERM);
	CHECK_ERR(prctl(PR_SET_PDEATHSIG, 99, 0, 0, 0), EINVAL);
	CHECK(prctl(PR_SET_PDEATHSIG, 0, 0, 0, 0) == 0);

	/* no_new_privs can be set, never cleared. */
	CHECK(prctl(PR_GET_NO_NEW_PRIVS, 0, 0, 0, 0) == 0);
	CHECK_ERR(prctl(PR_SET_NO_NEW_PRIVS, 1, 1, 0, 0), EINVAL);
	CHECK(prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0);
	CHECK(prctl(PR_GET_NO_NEW_PRIVS, 0, 0, 0, 0) == 1);
	CHECK_ERR(prctl(PR_SET_NO_NEW_PRIVS, 0, 0, 0, 0), EINVAL);

	/* Timer slack: 50us by default, 0 restores the default. */
	CHECK(prctl(PR_GET_TIMERSLACK, 0, 0, 0, 0) == 50000);
	CHECK(prctl(PR_SET_TIMERSLACK, 1000, 0, 0, 0) == 0);
	CHECK(prctl(PR_GET_TIMERSLACK, 0, 0, 0, 0) == 1000);
	CHECK(prctl(PR_SET_TIMERSLACK, 0, 0, 0, 0) == 0);
	CHECK(prctl(PR_GET_TIMERSLACK, 0, 0, 0, 0) == 50000);

	int reaper = -1;
	CHECK(prctl(PR_GET_CHILD_SUBREAPER, &reaper, 0, 0, 0) == 0 && reaper == 0);
	CHECK(prctl(PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0) == 0);
	CHECK(prctl(PR_GET_CHILD_SUBREAPER, &reaper, 0, 0, 0) == 0 && reaper == 1);

	CHECK(prctl(PR_GET_KEEPCAPS, 0, 0, 0, 0) == 0);
	CHECK(prctl(PR_SET_KEEPCAPS, 1, 0, 0, 0) == 0);
	CHECK(prctl(PR_GET_KEEPCAPS, 0, 0, 0, 0) == 1);
	CHECK_ERR(prctl(PR_SET_KEEPCAPS, 2, 0, 0, 0), EINVAL);

	CHECK(prctl(PR_CAPBSET_READ, 0, 0, 0, 0) == 1);
	CHECK_ERR(prctl(PR_CAPBSET_READ, 999, 0, 0, 0), EINVAL);

	CHECK_ERR(prctl(9999, 0, 0, 0, 0), EINVAL);

	if (failures)
		return 1;
	puts("ok");
	return 0;
}
