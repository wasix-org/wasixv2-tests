// Thread-local storage: a new thread starts from the initial image, its writes stay its own,
// errno is per thread, and pthread_self differs between threads.
//#ExpectedStdout: thread: init=7 own=101 errno=0 self_differs=1
//#ExpectedStdout: main: tls=1 errno=9
//#ExpectedExitCode: 0
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

static _Thread_local int tls = 7;
static pthread_t main_self;
static int init, own, terr, differs;

static void *run(void *arg)
{
	init = tls;
	tls = 101;
	own = tls;
	terr = errno;
	differs = !pthread_equal(pthread_self(), main_self);
	return 0;
}

int main(void)
{
	main_self = pthread_self();
	tls = 1;
	close(-1); /* EBADF */
	pthread_t t;
	pthread_create(&t, 0, run, 0);
	pthread_join(t, 0);
	printf("thread: init=%d own=%d errno=%d self_differs=%d\n", init, own, terr, differs);
	printf("main: tls=%d errno=%d\n", tls, errno);
	return 0;
}
