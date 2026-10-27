// A robust process-shared mutex in a SysV segment, held by a thread of a child process whose
// main thread calls exit (exit_group): every task's exit walks its robust list, so the thread
// the group exit ends marks the mutex FUTEX_OWNER_DIED before the segment is written back, and
// the parent's trylock gets EOWNERDEAD, makes the mutex consistent and can use it again.
//#MappedDirectory: .:/t
//#ExpectedStdout: child=0 trylock=EOWNERDEAD consistent=0 relock=0
//#ExpectedExitCode: 0
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <spawn.h>
#include <stdio.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

#define SHM_KEY 0x7b7b

static volatile int holding;

static void *holder(void *arg)
{
	pthread_mutex_t *m = arg;
	if (pthread_mutex_lock(m) == 0)
		holding = 1;
	for (;;)
		pause();
	return 0;
}

static int child(void)
{
	int shm = shmget(SHM_KEY, 0, 0);
	pthread_mutex_t *m = shmat(shm, NULL, 0);
	if (shm < 0 || m == (void *)-1)
		return 2;
	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
	pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);
	pthread_mutex_init(m, &attr);
	pthread_t t;
	pthread_create(&t, 0, holder, m);
	while (!holding) {
		struct timespec d = {0, 5 * 1000 * 1000};
		nanosleep(&d, 0);
	}
	/* exit_group, with the holder still in its loop. */
	exit(0);
}

static const char *name(int e)
{
	switch (e) {
	case 0: return "0";
	case EOWNERDEAD: return "EOWNERDEAD";
	case EBUSY: return "EBUSY";
	case ENOTRECOVERABLE: return "ENOTRECOVERABLE";
	default: return strerror(e);
	}
}

int main(int argc, char **argv)
{
	if (argc > 1)
		return child();

	int shm = shmget(SHM_KEY, sizeof(pthread_mutex_t), IPC_CREAT | IPC_EXCL | 0600);
	char *argv2[] = {"/t/main", "child", NULL};
	pid_t pid;
	int status = -1;
	if (shm < 0 || posix_spawn(&pid, "/t/main", NULL, NULL, argv2, environ) != 0) {
		printf("setup failed: %s\n", strerror(errno));
		return 1;
	}
	waitpid(pid, &status, 0);
	pthread_mutex_t *m = shmat(shm, NULL, 0);
	int trylock = pthread_mutex_trylock(m);
	int consistent = trylock == EOWNERDEAD ? pthread_mutex_consistent(m) : -1;
	pthread_mutex_unlock(m);
	int relock = pthread_mutex_trylock(m);
	pthread_mutex_unlock(m);
	printf("child=%d trylock=%s consistent=%d relock=%s\n",
	       WIFEXITED(status) ? WEXITSTATUS(status) : -1, name(trylock), consistent, name(relock));
	shmdt(m);
	shmctl(shm, IPC_RMID, NULL);
	return 0;
}
