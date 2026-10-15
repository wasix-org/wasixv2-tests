// exit() in a thread ends the whole process with its status while main is blocked in
// pthread_join; main never gets past the join.
//#ExpectedStdout: thread exiting
//#ExpectedExitCode: 7
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

static void *run(void *arg)
{
	printf("thread exiting\n");
	fflush(stdout);
	exit(7);
}

int main(void)
{
	pthread_t t;
	pthread_create(&t, 0, run, 0);
	pthread_join(t, 0);
	printf("main survived\n");
	return 1;
}
