// A blocking write into a full pipe whose last reader leaves meanwhile returns what went in and
// still raises SIGPIPE, as Linux's pipe_write does whenever the pipe has no readers.
//#ExpectedStdout: write 4096 handler 13
//#ExpectedStdout: write 65536 handler 13
//#ExpectedExitCode: 0
#define _GNU_SOURCE
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static volatile int got;
static void handler(int sig) { got = sig; }

static void *close_later(void *arg)
{
	usleep(100 * 1000);
	close(*(int *)arg);
	return NULL;
}

/* Fills a pipe of `size` bytes with a write of `len`, which waits until the reader leaves. */
static void run(int size, size_t len)
{
	int fds[2];
	pthread_t thread;
	if (pipe(fds) || fcntl(fds[1], F_SETPIPE_SZ, size) != size)
		exit(1);
	char *buf = calloc(1, len);
	got = 0;
	pthread_create(&thread, NULL, close_later, &fds[0]);
	ssize_t n = write(fds[1], buf, len);
	pthread_join(thread, NULL);
	printf("write %zd handler %d\n", n, got);
	close(fds[1]);
	free(buf);
}

int main(void)
{
	signal(SIGPIPE, handler);
	/* Cut short within one step of the pipe, and between the kernel's 64 KiB steps. */
	run(4096, 8192);
	run(65536, 65536 + 100);
	return 0;
}
