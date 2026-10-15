// Detached threads (by attribute and by pthread_detach) run and clean up after themselves.
//#ExpectedStdout: done=20
//#ExpectedExitCode: 0
#include <pthread.h>
#include <stdio.h>
#include <sched.h>

static int done;

static void *run(void *arg)
{
	__atomic_fetch_add(&done, 1, __ATOMIC_SEQ_CST);
	return 0;
}

int main(void)
{
	pthread_attr_t a;
	pthread_attr_init(&a);
	pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
	for (int i = 0; i < 10; i++) {
		pthread_t t;
		if (pthread_create(&t, &a, run, 0)) return 1;
	}
	for (int i = 0; i < 10; i++) {
		pthread_t t;
		if (pthread_create(&t, 0, run, 0)) return 2;
		if (pthread_detach(t)) return 3;
	}
	while (__atomic_load_n(&done, __ATOMIC_SEQ_CST) < 20) sched_yield();
	printf("done=%d\n", done);
	return 0;
}
