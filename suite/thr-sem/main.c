// Unnamed POSIX semaphores between threads: sem_post wakes sem_wait, sem_trywait fails with
// EAGAIN at zero, and sem_timedwait times out with ETIMEDOUT.
//#ExpectedStdout: got=10 trywait=EAGAIN timedwait=ETIMEDOUT
//#ExpectedExitCode: 0
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <time.h>

static sem_t s;

static void *producer(void *arg)
{
	for (int i = 0; i < 10; i++) sem_post(&s);
	return 0;
}

int main(void)
{
	sem_init(&s, 0, 0);
	pthread_t t;
	pthread_create(&t, 0, producer, 0);
	int got = 0;
	for (int i = 0; i < 10; i++)
		if (!sem_wait(&s)) got++;
	pthread_join(t, 0);
	int r = sem_trywait(&s);
	const char *tw = r == -1 && errno == EAGAIN ? "EAGAIN" : "?";
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_nsec += 50 * 1000 * 1000;
	if (ts.tv_nsec >= 1000000000) {
		ts.tv_sec++;
		ts.tv_nsec -= 1000000000;
	}
	r = sem_timedwait(&s, &ts);
	printf("got=%d trywait=%s timedwait=%s\n", got, tw,
		r == -1 && errno == ETIMEDOUT ? "ETIMEDOUT" : "?");
	return 0;
}
