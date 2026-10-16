// A timer signal that is still pending when its timer is deleted or re-armed is stale and is
// never delivered; a deleted timer's id is invalid; ignored periodic timer signals come back
// once the signal is caught again; a forked child inherits no timers.
//#ExpectedStdout: ok
#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
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

#define MS 1000000LL

static sigset_t set;

static timer_t make(int value)
{
	struct sigevent sev = {.sigev_notify = SIGEV_SIGNAL, .sigev_signo = SIGUSR1};
	sev.sigev_value.sival_int = value;
	timer_t t;
	CHECK(timer_create(CLOCK_MONOTONIC, &sev, &t) == 0);
	return t;
}

static void arm(timer_t t, long long value, long long interval)
{
	struct itimerspec spec = {
		.it_value = {value / 1000000000LL, value % 1000000000LL},
		.it_interval = {interval / 1000000000LL, interval % 1000000000LL},
	};
	CHECK(timer_settime(t, 0, &spec, NULL) == 0);
}

static int take(int *value)
{
	struct timespec zero = {0, 0};
	siginfo_t si;
	int sig = sigtimedwait(&set, &si, &zero);
	if (sig > 0 && value)
		*value = si.si_value.sival_int;
	return sig;
}

int main(void)
{
	sigemptyset(&set);
	sigaddset(&set, SIGUSR1);
	CHECK(sigprocmask(SIG_BLOCK, &set, NULL) == 0);

	/* Deleted while pending. */
	timer_t t = make(1);
	arm(t, MS, 0);
	usleep(20000);
	CHECK(timer_delete(t) == 0);
	errno = 0;
	CHECK(take(NULL) == -1 && errno == EAGAIN);
	struct itimerspec cur;
	errno = 0;
	CHECK(timer_gettime(t, &cur) == -1 && errno == EINVAL);
	errno = 0;
	CHECK(timer_delete(t) == -1 && errno == EINVAL);

	/* Re-armed while pending: only the new setting's signal counts. */
	t = make(2);
	arm(t, MS, 0);
	usleep(20000);
	arm(t, 10 * MS, 0);
	errno = 0;
	CHECK(take(NULL) == -1 && errno == EAGAIN);
	usleep(30000);
	int value = 0;
	CHECK(take(&value) == SIGUSR1 && value == 2);
	CHECK(timer_delete(t) == 0);

	/* SIG_IGN while a periodic timer fires; caught again, its signal is back. */
	CHECK(sigprocmask(SIG_UNBLOCK, &set, NULL) == 0);
	signal(SIGUSR1, SIG_IGN);
	timer_t periodic = make(3);
	timer_t once = make(4);
	arm(periodic, MS, 5 * MS);
	arm(once, MS, 0);
	usleep(30000);
	CHECK(sigprocmask(SIG_BLOCK, &set, NULL) == 0);
	errno = 0;
	CHECK(take(NULL) == -1 && errno == EAGAIN);
	signal(SIGUSR1, SIG_DFL);
	value = 0;
	CHECK(take(&value) == SIGUSR1 && value == 3);
	CHECK(timer_delete(once) == 0);

	/* fork: the child has none of the parent's timers. */
	pid_t pid = fork();
	if (pid == 0) {
		struct itimerspec child;
		int r = timer_gettime(periodic, &child);
		_exit(r == -1 && errno == EINVAL ? 0 : 1);
	}
	int status;
	CHECK(waitpid(pid, &status, 0) == pid);
	CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
	CHECK(timer_gettime(periodic, &cur) == 0);
	CHECK(timer_delete(periodic) == 0);

	if (failures)
		return 1;
	puts("ok");
	return 0;
}
