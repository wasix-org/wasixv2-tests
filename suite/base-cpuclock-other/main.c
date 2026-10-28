// The CPU clocks of other processes and threads, as clock_getcpuclockid and
// pthread_getcpuclockid name them: clock_gettime, clock_getres, timer_create and
// clock_nanosleep on them, and the errors Linux gives (posix-cpu-timers.c, pid_for_clock):
// EINVAL for a pid that names nothing, for a thread of another process, and for a thread's
// own clock in clock_nanosleep; ESRCH from timer_settime once the clock's process is reaped.
//#MappedDirectory: .:/t
//#ExpectedStdout: ok
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

static int failures;
#define CHECK(cond)                                                                       \
	do {                                                                              \
		if (!(cond)) {                                                            \
			printf("FAIL line %d: %s (errno %d: %s)\n", __LINE__, #cond, errno, \
			       strerror(errno));                                          \
			failures++;                                                       \
		}                                                                         \
	} while (0)

static long long ns(struct timespec ts)
{
	return ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static long long now(clockid_t clock)
{
	struct timespec ts;
	if (clock_gettime(clock, &ts) != 0)
		return -1;
	return ns(ts);
}

/* Burns CPU time until the monotonic clock advanced by `ms` or `stop` is set. */
static void spin(int ms, atomic_int *stop)
{
	long long end = now(CLOCK_MONOTONIC) + ms * 1000000LL;
	volatile unsigned x = 0;
	while (now(CLOCK_MONOTONIC) < end && !(stop && atomic_load(stop)))
		for (int i = 0; i < 1000; i++)
			x += i;
}

static atomic_int stop_thread;

static void *spinner(void *arg)
{
	spin(5000, &stop_thread);
	return arg;
}

/* `make_thread_cpuclock(tid, CPUCLOCK_SCHED)`. */
static clockid_t thread_clock(pid_t tid)
{
	return (clockid_t)((~(unsigned)tid << 3) | 6);
}

static int sleep_on(clockid_t clock, int ms)
{
	struct timespec t = {0, ms * 1000000L};
	return clock_nanosleep(clock, 0, &t, NULL);
}

int main(int argc, char **argv)
{
	if (argc > 1 && strcmp(argv[1], "spin") == 0) {
		spin(5000, NULL);
		return 0;
	}

	/* init's clock, as posix clock_getcpuclockid/1-2 reads it. */
	clockid_t cid;
	struct timespec ts, res;
	CHECK(clock_getcpuclockid(1, &cid) == 0);
	CHECK(clock_gettime(cid, &ts) == 0);
	CHECK(clock_getcpuclockid(999999, &cid) == ESRCH);

	char *args[] = {"/t/main", "spin", NULL};
	pid_t child;
	if (posix_spawn(&child, "/t/main", NULL, NULL, args, environ)) {
		puts("FAIL posix_spawn");
		return 1;
	}
	clockid_t child_clock;
	CHECK(clock_getcpuclockid(child, &child_clock) == 0);
	CHECK(clock_getres(child_clock, &res) == 0 && res.tv_sec == 0 && res.tv_nsec > 0);
	long long t0 = now(child_clock);
	struct timespec pause = {0, 50 * 1000000L};
	nanosleep(&pause, NULL);
	long long t1 = now(child_clock);
	CHECK(t0 >= 0 && t1 > t0);
	/* A thread clock names a thread of the caller's own process only. */
	errno = 0;
	CHECK(clock_gettime(thread_clock(child), &ts) == -1 && errno == EINVAL);

	/* A timer on the child's CPU clock signals us. */
	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, SIGUSR1);
	sigprocmask(SIG_BLOCK, &set, NULL);
	struct sigevent event = {.sigev_notify = SIGEV_SIGNAL, .sigev_signo = SIGUSR1};
	event.sigev_value.sival_int = 42;
	timer_t timer;
	CHECK(timer_create(child_clock, &event, &timer) == 0);
	struct itimerspec spec = {.it_value = {0, 30 * 1000000L}};
	CHECK(timer_settime(timer, 0, &spec, NULL) == 0);
	siginfo_t info;
	struct timespec wait = {5, 0};
	CHECK(sigtimedwait(&set, &info, &wait) == SIGUSR1);
	CHECK(info.si_code == SI_TIMER && info.si_value.sival_int == 42);

	/* clock_nanosleep waits for the child's clock. */
	long long before = now(child_clock);
	CHECK(sleep_on(child_clock, 30) == 0);
	CHECK(now(child_clock) - before >= 30 * 1000000LL);

	/* Another thread's clock: readable, and one can sleep on it, but not on one's own. */
	pthread_t thread;
	CHECK(pthread_create(&thread, NULL, spinner, NULL) == 0);
	clockid_t other;
	CHECK(pthread_getcpuclockid(thread, &other) == 0);
	CHECK(clock_gettime(other, &ts) == 0);
	CHECK(sleep_on(other, 20) == 0);
	clockid_t mine;
	CHECK(pthread_getcpuclockid(pthread_self(), &mine) == 0);
	CHECK(sleep_on(mine, 1) == EINVAL);
	CHECK(sleep_on(CLOCK_THREAD_CPUTIME_ID, 1) == EINVAL);
	atomic_store(&stop_thread, 1);
	pthread_join(thread, NULL);

	/* Once the child is reaped, its pid names no clock. */
	kill(child, SIGKILL);
	int status;
	CHECK(waitpid(child, &status, 0) == child);
	errno = 0;
	CHECK(clock_gettime(child_clock, &ts) == -1 && errno == EINVAL);
	CHECK(clock_getcpuclockid(child, &cid) == ESRCH);
	errno = 0;
	CHECK(timer_settime(timer, 0, &spec, NULL) == -1 && errno == ESRCH);

	if (failures)
		return 1;
	puts("ok");
	return 0;
}
