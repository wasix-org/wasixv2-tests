// timerfd: one-shot, periodic and absolute timers, disarming, gettime and the old value from
// settime, blocking and non-blocking reads, readiness through poll, and the error cases.
//#ExpectedStdout: ok
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/timerfd.h>
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

static long long now(clockid_t clock)
{
	struct timespec ts;
	clock_gettime(clock, &ts);
	return ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static struct timespec at(long long ns)
{
	return (struct timespec){ns / 1000000000LL, ns % 1000000000LL};
}

static long long ns(struct timespec ts)
{
	return ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

int main(void)
{
	int fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
	CHECK(fd >= 0);
	CHECK(fcntl(fd, F_GETFD) == FD_CLOEXEC);
	CHECK((fcntl(fd, F_GETFL) & O_NONBLOCK) != 0);

	uint64_t ticks = 0;
	errno = 0;
	CHECK(read(fd, &ticks, 8) == -1 && errno == EAGAIN);

	struct itimerspec cur;
	CHECK(timerfd_gettime(fd, &cur) == 0);
	CHECK(ns(cur.it_value) == 0 && ns(cur.it_interval) == 0);

	struct pollfd pfd = {fd, POLLIN, 0};
	CHECK(poll(&pfd, 1, 0) == 0);

	/* One-shot. */
	struct itimerspec spec = {.it_value = at(30 * MS)};
	long long start = now(CLOCK_MONOTONIC);
	CHECK(timerfd_settime(fd, 0, &spec, NULL) == 0);
	CHECK(timerfd_gettime(fd, &cur) == 0);
	CHECK(ns(cur.it_value) > 0 && ns(cur.it_value) <= 30 * MS && ns(cur.it_interval) == 0);
	CHECK(poll(&pfd, 1, 2000) == 1 && pfd.revents == POLLIN);
	CHECK(now(CLOCK_MONOTONIC) - start >= 30 * MS);
	CHECK(read(fd, &ticks, 8) == 8 && ticks == 1);
	errno = 0;
	CHECK(read(fd, &ticks, 8) == -1 && errno == EAGAIN);
	CHECK(timerfd_gettime(fd, &cur) == 0 && ns(cur.it_value) == 0);

	/* A short buffer is EINVAL. */
	char small[4];
	errno = 0;
	CHECK(read(fd, small, sizeof small) == -1 && errno == EINVAL);

	/* Periodic: expirations accumulate between reads. */
	spec = (struct itimerspec){.it_value = at(10 * MS), .it_interval = at(10 * MS)};
	CHECK(timerfd_settime(fd, 0, &spec, NULL) == 0);
	usleep(65000);
	CHECK(read(fd, &ticks, 8) == 8 && ticks >= 4 && ticks <= 8);
	CHECK(timerfd_gettime(fd, &cur) == 0);
	CHECK(ns(cur.it_interval) == 10 * MS && ns(cur.it_value) > 0 &&
	      ns(cur.it_value) <= 10 * MS);

	/* Re-arming reports the old setting; disarming stops it. */
	struct itimerspec old;
	spec = (struct itimerspec){0};
	CHECK(timerfd_settime(fd, 0, &spec, &old) == 0);
	CHECK(ns(old.it_interval) == 10 * MS);
	CHECK(timerfd_gettime(fd, &cur) == 0 && ns(cur.it_value) == 0 && ns(cur.it_interval) == 0);
	usleep(25000);
	errno = 0;
	CHECK(read(fd, &ticks, 8) == -1 && errno == EAGAIN);

	/* Absolute deadline on the monotonic clock. */
	long long deadline = now(CLOCK_MONOTONIC) + 20 * MS;
	spec = (struct itimerspec){.it_value = at(deadline)};
	CHECK(timerfd_settime(fd, TFD_TIMER_ABSTIME, &spec, NULL) == 0);
	CHECK(poll(&pfd, 1, 2000) == 1);
	CHECK(now(CLOCK_MONOTONIC) >= deadline);
	CHECK(read(fd, &ticks, 8) == 8 && ticks == 1);

	/* An absolute deadline in the past fires at once. */
	spec = (struct itimerspec){.it_value = at(1)};
	CHECK(timerfd_settime(fd, TFD_TIMER_ABSTIME, &spec, NULL) == 0);
	CHECK(read(fd, &ticks, 8) == 8 && ticks == 1);
	close(fd);

	/* Blocking read on a realtime timer. */
	fd = timerfd_create(CLOCK_REALTIME, 0);
	CHECK(fd >= 0);
	spec = (struct itimerspec){.it_value = at(25 * MS)};
	start = now(CLOCK_MONOTONIC);
	CHECK(timerfd_settime(fd, 0, &spec, NULL) == 0);
	CHECK(read(fd, &ticks, 8) == 8 && ticks == 1);
	CHECK(now(CLOCK_MONOTONIC) - start >= 25 * MS);
	close(fd);

	fd = timerfd_create(CLOCK_BOOTTIME, 0);
	CHECK(fd >= 0);
	close(fd);

	/* Errors. */
	errno = 0;
	CHECK(timerfd_create(100, 0) == -1 && errno == EINVAL);
	errno = 0;
	CHECK(timerfd_create(CLOCK_PROCESS_CPUTIME_ID, 0) == -1 && errno == EINVAL);
	errno = 0;
	CHECK(timerfd_create(CLOCK_MONOTONIC, 0x1234) == -1 && errno == EINVAL);
	fd = timerfd_create(CLOCK_MONOTONIC, 0);
	spec = (struct itimerspec){.it_value = {0, 1000000000}};
	errno = 0;
	CHECK(timerfd_settime(fd, 0, &spec, NULL) == -1 && errno == EINVAL);
	spec = (struct itimerspec){.it_value = at(MS)};
	errno = 0;
	CHECK(timerfd_settime(fd, 0x40, &spec, NULL) == -1 && errno == EINVAL);
	errno = 0;
	CHECK(timerfd_settime(0, 0, &spec, NULL) == -1 && errno == EINVAL);
	errno = 0;
	CHECK(timerfd_gettime(0, &cur) == -1 && errno == EINVAL);
	errno = 0;
	CHECK(timerfd_gettime(999, &cur) == -1 && errno == EBADF);
	close(fd);

	if (failures)
		return 1;
	puts("ok");
	return 0;
}
