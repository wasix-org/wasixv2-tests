// A barrier holds every thread until all arrived, over several rounds; exactly one thread per
// round gets PTHREAD_BARRIER_SERIAL_THREAD.
//#ExpectedStdout: serial=5 ok=1
//#ExpectedExitCode: 0
#include <pthread.h>
#include <stdio.h>

#define N 4
#define ROUNDS 5
static pthread_barrier_t b;
static int arrived[ROUNDS], serial, bad;

static void *run(void *arg)
{
	for (int r = 0; r < ROUNDS; r++) {
		__atomic_fetch_add(&arrived[r], 1, __ATOMIC_SEQ_CST);
		int ret = pthread_barrier_wait(&b);
		if (ret == PTHREAD_BARRIER_SERIAL_THREAD) __atomic_fetch_add(&serial, 1, __ATOMIC_SEQ_CST);
		else if (ret) bad = 1;
		if (__atomic_load_n(&arrived[r], __ATOMIC_SEQ_CST) != N) bad = 1;
	}
	return 0;
}

int main(void)
{
	pthread_barrier_init(&b, 0, N);
	pthread_t t[N];
	for (int i = 0; i < N; i++) pthread_create(&t[i], 0, run, 0);
	for (int i = 0; i < N; i++) pthread_join(t[i], 0);
	printf("serial=%d ok=%d\n", serial, !bad);
	return 0;
}
