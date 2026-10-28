// Process accounting: computing raises the CPU clocks and ru_utime, sleeping does not, a
// syscall loop shows as system time in times() and getrusage, a dead thread's time stays with
// its process, and a reaped child's goes to its parent's cutime and cstime and to wait4's
// rusage. ITIMER_VIRTUAL and ITIMER_PROF fire for a busy process (user, and user or system
// time) and not for a sleeping one (kernel/time/itimer.c, posix-cpu-timers.c).
//#MappedDirectory: .:/t
//#ExpectedStdout: ok
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/times.h>
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

#define MS 1000000LL

static long long now(clockid_t clock)
{
	struct timespec ts;
	if (clock_gettime(clock, &ts) != 0)
		return -1;
	return ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static long long tv_ns(struct timeval tv)
{
	return tv.tv_sec * 1000000000LL + tv.tv_usec * 1000LL;
}

/* Computes until `clock` advanced by `ms` of CPU time, or `*stop` is set, or 10 s went by. */
static void spin(clockid_t clock, int ms, volatile sig_atomic_t *stop)
{
	long long end = now(clock) + ms * MS;
	long long give_up = now(CLOCK_MONOTONIC) + 10000 * MS;
	volatile unsigned x = 0;
	while (now(clock) < end && !(stop && *stop) && now(CLOCK_MONOTONIC) < give_up)
		for (int i = 0; i < 100000; i++)
			x += i;
}

static volatile sig_atomic_t vtalrm, prof;

static void on_signal(int sig)
{
	if (sig == SIGVTALRM)
		vtalrm++;
	else
		prof++;
}

static void *thread_spin(void *arg)
{
	spin(CLOCK_THREAD_CPUTIME_ID, 100, NULL);
	*(long long *)arg = now(CLOCK_THREAD_CPUTIME_ID);
	return NULL;
}

int main(int argc, char **argv)
{
	if (argc > 1 && strcmp(argv[1], "spin") == 0) {
		spin(CLOCK_PROCESS_CPUTIME_ID, 100, NULL);
		return 0;
	}

	/* Computing raises the CPU clocks and user time. */
	struct rusage ru0, ru1;
	CHECK(getrusage(RUSAGE_SELF, &ru0) == 0);
	long long p0 = now(CLOCK_PROCESS_CPUTIME_ID), t0 = now(CLOCK_THREAD_CPUTIME_ID);
	spin(CLOCK_PROCESS_CPUTIME_ID, 100, NULL);
	long long p1 = now(CLOCK_PROCESS_CPUTIME_ID), t1 = now(CLOCK_THREAD_CPUTIME_ID);
	CHECK(getrusage(RUSAGE_SELF, &ru1) == 0);
	CHECK(p1 - p0 >= 100 * MS);
	CHECK(t1 - t0 >= 100 * MS);
	CHECK(tv_ns(ru1.ru_utime) - tv_ns(ru0.ru_utime) >= 50 * MS);
	CHECK(ru1.ru_maxrss > 0);

	/* Sleeping does not. */
	struct timespec pause = {0, 200 * MS};
	p0 = now(CLOCK_PROCESS_CPUTIME_ID);
	CHECK(nanosleep(&pause, NULL) == 0);
	CHECK(now(CLOCK_PROCESS_CPUTIME_ID) - p0 < 50 * MS);

	/* A loop of syscalls is system time. */
	struct tms tms0, tms1;
	CHECK(times(&tms0) != (clock_t)-1);
	CHECK(getrusage(RUSAGE_SELF, &ru0) == 0);
	int fd = open("/dev/zero", O_RDONLY);
	CHECK(fd >= 0);
	char byte;
	long long end = now(CLOCK_PROCESS_CPUTIME_ID) + 200 * MS;
	while (now(CLOCK_PROCESS_CPUTIME_ID) < end)
		for (int i = 0; i < 1000; i++)
			CHECK(read(fd, &byte, 1) == 1);
	close(fd);
	CHECK(times(&tms1) != (clock_t)-1);
	CHECK(getrusage(RUSAGE_SELF, &ru1) == 0);
	CHECK(tms1.tms_stime > tms0.tms_stime);
	CHECK(tv_ns(ru1.ru_stime) > tv_ns(ru0.ru_stime));

	/* A thread's time is its own, and stays with the process once it is gone. */
	long long thread_time = 0;
	pthread_t thread;
	p0 = now(CLOCK_PROCESS_CPUTIME_ID);
	CHECK(pthread_create(&thread, NULL, thread_spin, &thread_time) == 0);
	CHECK(pthread_join(thread, NULL) == 0);
	CHECK(thread_time >= 100 * MS);
	CHECK(now(CLOCK_PROCESS_CPUTIME_ID) - p0 >= 100 * MS);

	/* ITIMER_VIRTUAL fires for a busy process, not for a sleeping one. */
	CHECK(signal(SIGVTALRM, on_signal) != SIG_ERR);
	CHECK(signal(SIGPROF, on_signal) != SIG_ERR);
	struct itimerval it = {.it_value = {0, 50000}}, old;
	CHECK(setitimer(ITIMER_VIRTUAL, &it, NULL) == 0);
	CHECK(getitimer(ITIMER_VIRTUAL, &old) == 0);
	CHECK(old.it_value.tv_sec == 0 && old.it_value.tv_usec > 0 &&
	      old.it_value.tv_usec <= 50000 + 10000);
	CHECK(nanosleep(&pause, NULL) == 0);
	CHECK(vtalrm == 0);
	spin(CLOCK_PROCESS_CPUTIME_ID, 5000, &vtalrm);
	CHECK(vtalrm == 1);
	CHECK(getitimer(ITIMER_VIRTUAL, &old) == 0);
	CHECK(old.it_value.tv_sec == 0 && old.it_value.tv_usec == 0);

	/* ITIMER_PROF counts system time too, and repeats by its interval. */
	it.it_interval.tv_usec = 20000;
	CHECK(setitimer(ITIMER_PROF, &it, NULL) == 0);
	fd = open("/dev/zero", O_RDONLY);
	long long give_up = now(CLOCK_MONOTONIC) + 10000 * MS;
	while (prof < 3 && now(CLOCK_MONOTONIC) < give_up)
		read(fd, &byte, 1);
	close(fd);
	struct itimerval off = {0};
	CHECK(setitimer(ITIMER_PROF, &off, &old) == 0);
	CHECK(prof >= 3);
	CHECK(old.it_interval.tv_sec == 0 && old.it_interval.tv_usec == 20000);

	/* A reaped child's time goes to wait4's rusage and to the parent's children's. */
	char *args[] = {"/t/main", "spin", NULL};
	pid_t child;
	CHECK(posix_spawn(&child, "/t/main", NULL, NULL, args, environ) == 0);
	CHECK(times(&tms0) != (clock_t)-1 && tms0.tms_cutime == 0 && tms0.tms_cstime == 0);
	int status;
	struct rusage child_ru;
	CHECK(wait4(child, &status, 0, &child_ru) == child);
	CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
	long long child_time = tv_ns(child_ru.ru_utime) + tv_ns(child_ru.ru_stime);
	CHECK(child_time >= 90 * MS);
	CHECK(getrusage(RUSAGE_CHILDREN, &ru1) == 0);
	CHECK(tv_ns(ru1.ru_utime) + tv_ns(ru1.ru_stime) == child_time);
	CHECK(times(&tms1) != (clock_t)-1);
	CHECK(tms1.tms_cutime + tms1.tms_cstime >= 8);

	if (failures)
		return 1;
	puts("ok");
	return 0;
}
