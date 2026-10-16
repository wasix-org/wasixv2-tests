// Timers outlive what armed them and leave nothing behind when re-armed:
// - a timerfd re-armed 100000 times (an idle timeout reset per request) fires once, for the
//   last setting;
// - a timerfd armed by a thread that has exited still wakes a blocked read;
// - alarm() armed by a thread that has exited still sends SIGALRM;
// - a 1 ns periodic timer left unread counts its expirations without stalling the host.
//#ExpectedStdout: storm 1
//#ExpectedStdout: thread timerfd 1
//#ExpectedStdout: alarm 14
//#ExpectedStdout: many ok
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

static void arm(int fd, long long value_ns, long long interval_ns)
{
	struct itimerspec its = {
		.it_interval = {interval_ns / 1000000000LL, interval_ns % 1000000000LL},
		.it_value = {value_ns / 1000000000LL, value_ns % 1000000000LL},
	};
	if (timerfd_settime(fd, 0, &its, NULL)) perror("timerfd_settime");
}

static int thread_fd;

static void *arm_and_exit(void *arg)
{
	(void)arg;
	arm(thread_fd, 20 * 1000000LL, 0);
	alarm(1);
	return NULL;
}

static volatile sig_atomic_t alarmed;

static void handler(int sig)
{
	alarmed = sig;
}

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	uint64_t ticks = 0;

	int fd = timerfd_create(CLOCK_MONOTONIC, 0);
	for (int i = 0; i < 100000; i++)
		arm(fd, 30LL * 1000000000LL, 0);
	arm(fd, 10 * 1000000LL, 0);
	if (read(fd, &ticks, sizeof ticks) != sizeof ticks) perror("read");
	printf("storm %llu\n", (unsigned long long)ticks);
	close(fd);

	struct sigaction sa;
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = handler;
	sigaction(SIGALRM, &sa, 0);
	thread_fd = timerfd_create(CLOCK_MONOTONIC, 0);
	pthread_t thread;
	pthread_create(&thread, NULL, arm_and_exit, NULL);
	pthread_join(thread, NULL);
	ssize_t n;
	do {
		n = read(thread_fd, &ticks, sizeof ticks);
	} while (n < 0 && errno == EINTR);
	printf("thread timerfd %llu\n", (unsigned long long)ticks);
	while (!alarmed)
		pause();
	printf("alarm %d\n", (int)alarmed);

	fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
	arm(fd, 1, 1);
	struct timespec wait = {0, 50 * 1000000L};
	nanosleep(&wait, NULL);
	n = read(fd, &ticks, sizeof ticks);
	// About 50 million expirations; the count must be in that range, not truncated.
	printf("many %s\n", n == sizeof ticks && ticks > 10000000ULL && ticks < 10000000000ULL
				    ? "ok" : "bad");
	return 0;
}
