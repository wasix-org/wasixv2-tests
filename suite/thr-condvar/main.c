// Producer and consumer hand items over through a condition variable, and a broadcast wakes
// every waiter.
//#ExpectedStdout: consumed=100 sum=4950 woken=3
//#ExpectedExitCode: 0
#include <pthread.h>
#include <stdio.h>

static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t c = PTHREAD_COND_INITIALIZER;
static int item, have, consumed, sum, go, woken;

static void *consumer(void *arg)
{
	for (int i = 0; i < 100; i++) {
		pthread_mutex_lock(&m);
		while (!have) pthread_cond_wait(&c, &m);
		have = 0;
		consumed++;
		sum += item;
		pthread_cond_broadcast(&c);
		pthread_mutex_unlock(&m);
	}
	return 0;
}

static void *waiter(void *arg)
{
	pthread_mutex_lock(&m);
	while (!go) pthread_cond_wait(&c, &m);
	woken++;
	pthread_mutex_unlock(&m);
	return 0;
}

int main(void)
{
	pthread_t t;
	pthread_create(&t, 0, consumer, 0);
	for (int i = 0; i < 100; i++) {
		pthread_mutex_lock(&m);
		while (have) pthread_cond_wait(&c, &m);
		item = i;
		have = 1;
		pthread_cond_signal(&c);
		pthread_mutex_unlock(&m);
	}
	pthread_join(t, 0);

	pthread_t w[3];
	for (int i = 0; i < 3; i++) pthread_create(&w[i], 0, waiter, 0);
	pthread_mutex_lock(&m);
	go = 1;
	pthread_cond_broadcast(&c);
	pthread_mutex_unlock(&m);
	for (int i = 0; i < 3; i++) pthread_join(w[i], 0);
	printf("consumed=%d sum=%d woken=%d\n", consumed, sum, woken);
	return 0;
}
