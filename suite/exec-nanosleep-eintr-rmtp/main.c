// nanosleep interrupted by a handler fails with EINTR and writes the time left to rem. One
// interrupted by a signal without a handler (SIGTSTP, whose stop is not implemented and so does
// nothing) goes on to its original end instead of starting over (restart_syscall).
//#ExpectedStdout: eintr=1 rem ok=1
//#ExpectedStdout: restarted to the end=1
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>

static void on_alarm(int sig)
{
	(void)sig;
}

static long ms_since(const struct timespec *start)
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (now.tv_sec - start->tv_sec) * 1000 + (now.tv_nsec - start->tv_nsec) / 1000000;
}

static pthread_t sleeper;

static void *stop_sleeper(void *arg)
{
	struct timespec wait = {0, 300 * 1000 * 1000};
	nanosleep(&wait, 0);
	pthread_kill(sleeper, SIGTSTP);
	return arg;
}

int main(void)
{
	struct sigaction sa = {0};
	sa.sa_handler = on_alarm;
	sigaction(SIGALRM, &sa, 0);
	struct itimerval timer = {{0, 0}, {0, 200 * 1000}};
	setitimer(ITIMER_REAL, &timer, 0);
	struct timespec req = {1, 0}, rem = {0, 0};
	int ret = nanosleep(&req, &rem);
	long left = rem.tv_sec * 1000 + rem.tv_nsec / 1000000;
	printf("eintr=%d rem ok=%d\n", ret == -1 && errno == EINTR, left > 500 && left < 1000);

	sleeper = pthread_self();
	pthread_t t;
	struct timespec start;
	clock_gettime(CLOCK_MONOTONIC, &start);
	pthread_create(&t, 0, stop_sleeper, 0);
	req.tv_sec = 0;
	req.tv_nsec = 600 * 1000 * 1000;
	ret = nanosleep(&req, 0);
	long took = ms_since(&start);
	pthread_join(t, 0);
	// Starting over at 300 ms would take 900 ms.
	printf("restarted to the end=%d\n", ret == 0 && took >= 590 && took < 850);
	if (ret != 0 || took < 590 || took >= 850)
		printf("ret=%d took=%ld ms\n", ret, took);
	return 0;
}
