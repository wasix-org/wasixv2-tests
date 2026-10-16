// Two threads add two epolls into each other at the same time, many times over. The loop
// check and the insert are one step, so exactly one add of each pair succeeds and the other
// fails with ELOOP; no cycle is ever built (a wake through one would recurse forever).
//#ExpectedStdout: ok
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/epoll.h>
#include <unistd.h>

#define ROUNDS 500

static int a, b;
static pthread_barrier_t start, done;
static int result[2], error[2];

static int add(int epfd, int fd)
{
	struct epoll_event ev = {.events = EPOLLIN, .data.fd = fd};
	return epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
}

static void *other(void *arg)
{
	(void)arg;
	for (int i = 0; i < ROUNDS; i++) {
		pthread_barrier_wait(&start);
		result[1] = add(b, a);
		error[1] = errno;
		pthread_barrier_wait(&done);
	}
	return NULL;
}

int main(void)
{
	pthread_barrier_init(&start, NULL, 2);
	pthread_barrier_init(&done, NULL, 2);
	pthread_t thread;
	pthread_create(&thread, NULL, other, NULL);
	int failures = 0;
	for (int i = 0; i < ROUNDS; i++) {
		a = epoll_create1(0);
		b = epoll_create1(0);
		pthread_barrier_wait(&start);
		result[0] = add(a, b);
		error[0] = errno;
		pthread_barrier_wait(&done);
		int ok = (result[0] == 0) + (result[1] == 0);
		int loops = (result[0] == -1 && error[0] == ELOOP) +
			    (result[1] == -1 && error[1] == ELOOP);
		if (ok != 1 || loops != 1) {
			printf("round %d: %d/%d %d/%d\n", i, result[0], error[0], result[1], error[1]);
			failures++;
		}
		close(a);
		close(b);
	}
	pthread_join(thread, NULL);
	if (!failures) printf("ok\n");
	return failures != 0;
}
