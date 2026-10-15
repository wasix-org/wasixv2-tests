// A mutex serializes a non-atomic read-modify-write across threads.
//#ExpectedStdout: total=80000
//#ExpectedExitCode: 0
#include <pthread.h>
#include <stdio.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static volatile long total;

static void *run(void *arg)
{
	for (int i = 0; i < 20000; i++) {
		pthread_mutex_lock(&lock);
		long v = total;
		total = v + 1;
		pthread_mutex_unlock(&lock);
	}
	return 0;
}

int main(void)
{
	pthread_t t[4];
	for (int i = 0; i < 4; i++) pthread_create(&t[i], 0, run, 0);
	for (int i = 0; i < 4; i++) pthread_join(t[i], 0);
	printf("total=%ld\n", total);
	return 0;
}
