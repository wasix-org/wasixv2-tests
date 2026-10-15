// pthread_create and pthread_join: the thread runs in the same process with its own tid, sees
// its argument, and its return value reaches the joiner.
//#ExpectedStdout: arg=42 same_pid=1 other_tid=1 ret=43
//#ExpectedExitCode: 0
#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

static pid_t main_pid, main_tid;
static int same_pid, other_tid, seen;

static void *run(void *arg)
{
	same_pid = getpid() == main_pid;
	other_tid = gettid() != main_tid && gettid() > 0;
	seen = *(int *)arg;
	return (void *)(long)(*(int *)arg + 1);
}

int main(void)
{
	main_pid = getpid();
	main_tid = gettid();
	int arg = 42;
	pthread_t t;
	if (pthread_create(&t, 0, run, &arg)) return 1;
	void *ret;
	if (pthread_join(t, &ret)) return 2;
	printf("arg=%d same_pid=%d other_tid=%d ret=%ld\n", seen, same_pid, other_tid, (long)ret);
	return 0;
}
