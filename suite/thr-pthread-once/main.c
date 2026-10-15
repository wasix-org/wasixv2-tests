// pthread_once runs its routine exactly once although many threads race for it, and every
// caller returns only after it finished.
//#ExpectedStdout: calls=1 seen=8
//#ExpectedExitCode: 0
#include <pthread.h>
#include <stdio.h>

static pthread_once_t once = PTHREAD_ONCE_INIT;
static int calls, seen;

static void init(void) { __atomic_fetch_add(&calls, 1, __ATOMIC_SEQ_CST); }

static void *run(void *arg)
{
	pthread_once(&once, init);
	if (__atomic_load_n(&calls, __ATOMIC_SEQ_CST) == 1)
		__atomic_fetch_add(&seen, 1, __ATOMIC_SEQ_CST);
	return 0;
}

int main(void)
{
	pthread_t t[8];
	for (int i = 0; i < 8; i++) pthread_create(&t[i], 0, run, 0);
	for (int i = 0; i < 8; i++) pthread_join(t[i], 0);
	printf("calls=%d seen=%d\n", calls, seen);
	return 0;
}
