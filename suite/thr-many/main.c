// Many threads at once, each adding to a shared atomic counter, all joined.
//#ExpectedStdout: sum=32000
//#ExpectedExitCode: 0
#include <pthread.h>
#include <stdio.h>

#define N 32
static int counter;

static void *run(void *arg)
{
	for (int i = 0; i < 1000; i++) __atomic_fetch_add(&counter, 1, __ATOMIC_SEQ_CST);
	return arg;
}

int main(void)
{
	pthread_t t[N];
	for (int i = 0; i < N; i++)
		if (pthread_create(&t[i], 0, run, 0)) return 1;
	for (int i = 0; i < N; i++)
		if (pthread_join(t[i], 0)) return 2;
	printf("sum=%d\n", counter);
	return 0;
}
