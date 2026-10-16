// SIGEV_THREAD: libc runs the notify function on a thread of its own, which it drives through
// a SIGEV_THREAD_ID timer, with the sigevent value. An interval timer calls it repeatedly, and
// timer_delete stops it. Two timers each get their own thread. SIGEV_THREAD_ID aimed at the
// calling thread delivers there.
//#ExpectedStdout: ok
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
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

#define MS 1000000LL

static atomic_int calls[2];
static atomic_int wrong_value;
static pthread_t main_thread;
static atomic_int on_main;

static void notify(union sigval v)
{
	int which = v.sival_int;
	if (which != 0 && which != 1) {
		wrong_value = 1;
		return;
	}
	if (pthread_equal(pthread_self(), main_thread))
		on_main = 1;
	calls[which]++;
}

static void wait_calls(int which, int n)
{
	for (int i = 0; i < 3000 && calls[which] < n; i++)
		usleep(1000);
}

int main(void)
{
	main_thread = pthread_self();
	timer_t t[2];
	for (int i = 0; i < 2; i++) {
		struct sigevent sev = {.sigev_notify = SIGEV_THREAD};
		sev.sigev_notify_function = notify;
		sev.sigev_value.sival_int = i;
		CHECK(timer_create(CLOCK_MONOTONIC, &sev, &t[i]) == 0);
	}

	struct itimerspec once = {.it_value = {0, 10 * MS}};
	struct itimerspec every = {.it_value = {0, 5 * MS}, .it_interval = {0, 5 * MS}};
	CHECK(timer_settime(t[0], 0, &once, NULL) == 0);
	CHECK(timer_settime(t[1], 0, &every, NULL) == 0);
	wait_calls(0, 1);
	wait_calls(1, 5);
	CHECK(calls[0] == 1);
	CHECK(calls[1] >= 5);
	CHECK(!wrong_value && !on_main);

	struct itimerspec cur;
	CHECK(timer_gettime(t[1], &cur) == 0);
	CHECK(cur.it_interval.tv_nsec == 5 * MS);
	CHECK(timer_getoverrun(t[1]) >= 0);

	CHECK(timer_delete(t[1]) == 0);
	usleep(20000);
	int seen = calls[1];
	usleep(50000);
	CHECK(calls[1] == seen);
	usleep(20000);
	CHECK(calls[0] == 1);
	CHECK(timer_delete(t[0]) == 0);

	/* SIGEV_THREAD_ID: the signal goes to the named thread. */
	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, SIGUSR2);
	CHECK(sigprocmask(SIG_BLOCK, &set, NULL) == 0);
	struct sigevent sev = {.sigev_notify = SIGEV_THREAD_ID, .sigev_signo = SIGUSR2};
	sev.sigev_notify_thread_id = gettid();
	sev.sigev_value.sival_int = 77;
	timer_t tid_timer;
	CHECK(timer_create(CLOCK_MONOTONIC, &sev, &tid_timer) == 0);
	CHECK(timer_settime(tid_timer, 0, &once, NULL) == 0);
	siginfo_t si;
	struct timespec wait = {2, 0};
	CHECK(sigtimedwait(&set, &si, &wait) == SIGUSR2);
	CHECK(si.si_code == SI_TIMER && si.si_value.sival_int == 77);
	CHECK(timer_delete(tid_timer) == 0);
	sev.sigev_notify_thread_id = 999999;
	errno = 0;
	CHECK(timer_create(CLOCK_MONOTONIC, &sev, &tid_timer) == -1 && errno == EINVAL);

	if (failures)
		return 1;
	puts("ok");
	return 0;
}
