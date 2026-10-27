// A thread waits in F_SETLKW on a descriptor that another thread closes meanwhile. When the lock
// comes free, fcntl_setlk notices the close, drops the lock it won (no close is left to release
// it) and fails with EBADF, so the file is not left locked.
//#ExpectedStdout: setlkw -1 EBADF
//#ExpectedStdout: after F_UNLCK
//#ExpectedExitCode: 0
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

static int waiter, holder;

static void *close_then_unlock(void *arg)
{
	(void)arg;
	usleep(100 * 1000);
	close(waiter);
	usleep(50 * 1000);
	struct flock unlock = { .l_type = F_UNLCK, .l_whence = SEEK_SET };
	fcntl(holder, F_OFD_SETLK, &unlock);
	return NULL;
}

int main(void)
{
	const char *path = "/tmp/setlk-race";
	close(open(path, O_CREAT | O_RDWR, 0600));
	waiter = open(path, O_RDWR);
	holder = open(path, O_RDWR);
	int probe = open(path, O_RDWR);
	struct flock lock = { .l_type = F_WRLCK, .l_whence = SEEK_SET };
	if (fcntl(holder, F_OFD_SETLK, &lock))
		return 1;

	pthread_t thread;
	pthread_create(&thread, NULL, close_then_unlock, NULL);
	errno = 0;
	int r = fcntl(waiter, F_SETLKW, &lock);
	printf("setlkw %d %s\n", r, errno == EBADF ? "EBADF" : "?");
	pthread_join(thread, NULL);

	struct flock test = { .l_type = F_WRLCK, .l_whence = SEEK_SET };
	if (fcntl(probe, F_OFD_GETLK, &test))
		return 1;
	printf("after %s\n", test.l_type == F_UNLCK ? "F_UNLCK" : "locked");
	return 0;
}
