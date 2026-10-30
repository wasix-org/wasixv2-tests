// kernel/sys.c __sys_setresuid/__sys_setresgid leave distinct filesystem ids intact on a
// no-op. fs/namei.c generic_permission must continue to use those per-thread ids.
//#User: 0:0
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fsuid.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: %s (errno %d: %s)\n", __LINE__, #c, errno, strerror(errno)); exit(1); } } while (0)

static pthread_barrier_t barrier;

static void rendezvous(void)
{
	int rc = pthread_barrier_wait(&barrier);
	CHECK(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
}

static void *worker(void *unused)
{
	(void)unused;
	CHECK(syscall(SYS_setfsuid, 1000) == 0);
	CHECK(syscall(SYS_setfsgid, 1000) == 0);
	rendezvous();
	rendezvous();
	for (int real = 0; real < 2; real++) {
		for (int saved = 0; saved < 2; saved++) {
			CHECK(syscall(SYS_setresuid, real ? 0 : -1, -1, saved ? 0 : -1) == 0);
			CHECK(syscall(SYS_setresgid, real ? 0 : -1, -1, saved ? 0 : -1) == 0);
			CHECK(syscall(SYS_setfsuid, -1) == 1000);
			CHECK(syscall(SYS_setfsgid, -1) == 1000);
			errno = 0;
			CHECK(open("/tmp/fsids-root", O_RDONLY) == -1 && errno == EACCES);
			int fd = open("/tmp/fsids-group", O_RDONLY);
			CHECK(fd >= 0);
			CHECK(close(fd) == 0);
		}
	}
	// Supplying the effective id explicitly synchronizes the filesystem id.
	CHECK(syscall(SYS_setresuid, -1, 0, -1) == 0);
	CHECK(syscall(SYS_setresgid, -1, 0, -1) == 0);
	CHECK(syscall(SYS_setfsuid, -1) == 0);
	CHECK(syscall(SYS_setfsgid, -1) == 0);
	int fd = open("/tmp/fsids-root", O_RDONLY);
	CHECK(fd >= 0);
	CHECK(close(fd) == 0);
	return NULL;
}

int main(void)
{
	CHECK(getuid() == 0);
	CHECK(setgroups(0, NULL) == 0);
	umask(0);
	int fd = open("/tmp/fsids-root", O_CREAT | O_EXCL | O_WRONLY, 0600);
	CHECK(fd >= 0);
	CHECK(close(fd) == 0);
	fd = open("/tmp/fsids-group", O_CREAT | O_EXCL | O_WRONLY, 0040);
	CHECK(fd >= 0);
	CHECK(fchown(fd, 0, 1000) == 0);
	CHECK(close(fd) == 0);
	CHECK(pthread_barrier_init(&barrier, NULL, 2) == 0);
	pthread_t thread;
	CHECK(pthread_create(&thread, NULL, worker, NULL) == 0);
	rendezvous();
	CHECK(syscall(SYS_setfsuid, -1) == 0);
	CHECK(syscall(SYS_setfsgid, -1) == 0);
	rendezvous();
	CHECK(pthread_join(thread, NULL) == 0);
	CHECK(pthread_barrier_destroy(&barrier) == 0);
	CHECK(syscall(SYS_setfsuid, -1) == 0);
	CHECK(syscall(SYS_setfsgid, -1) == 0);
	CHECK(unlink("/tmp/fsids-root") == 0);
	CHECK(unlink("/tmp/fsids-group") == 0);
	puts("PASS");
	return 0;
}
