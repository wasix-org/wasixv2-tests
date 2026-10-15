// Deferred cancellation: a thread spinning on pthread_testcancel is cancelled, its cleanup
// handler runs, and it joins as PTHREAD_CANCELED.
//#ExpectedStdout: cancel=0 canceled=1 cleanup=1
//#ExpectedExitCode: 0
#include <pthread.h>
#include <stdio.h>

static int started, cleanup;

static void on_cancel(void *arg) { cleanup = 1; }

static void *run(void *arg)
{
	pthread_cleanup_push(on_cancel, 0);
	__atomic_store_n(&started, 1, __ATOMIC_SEQ_CST);
	for (;;) pthread_testcancel();
	pthread_cleanup_pop(0);
	return 0;
}

int main(void)
{
	pthread_t t;
	pthread_create(&t, 0, run, 0);
	while (!__atomic_load_n(&started, __ATOMIC_SEQ_CST))
		;
	int r = pthread_cancel(t);
	void *ret;
	pthread_join(t, &ret);
	printf("cancel=%d canceled=%d cleanup=%d\n", r, ret == PTHREAD_CANCELED, cleanup);
	return 0;
}
