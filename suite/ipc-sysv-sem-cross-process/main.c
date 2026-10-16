// SysV semaphores between processes started with posix_spawn: a child blocks in semop until
// the parent posts, GETNCNT counts it while it waits, SEM_UNDO adjustments are reverted when it
// exits, GETPID names the last process to operate, semtimedop times out with EAGAIN, and
// IPC_RMID wakes sleepers with EIDRM.
//#MappedDirectory: .:/t
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

#define KEY 0x5e5a

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%d line %d: CHECK(%s) failed (errno %s)\n", getpid(), __LINE__, #c, strerror(errno)); fails++; } } while (0)
#define FAILS(call, e) do { errno = 0; long r_ = (long)(call); if (r_ != -1 || errno != (e)) { fprintf(stderr, "line %d: %s = %ld (%s), want -1 %s\n", __LINE__, #call, r_, strerror(errno), #e); fails++; } } while (0)

static int op(int id, unsigned short num, short delta, short flags)
{
	struct sembuf sop = {num, delta, flags};
	return semop(id, &sop, 1);
}

static int child(const char *what)
{
	int id = semget(KEY, 0, 0);
	CHECK(id >= 0);
	if (strcmp(what, "post") == 0) {
		/* Wait for the parent, then leave one adjustment behind and one that stays. */
		CHECK(op(id, 0, -1, 0) == 0);
		CHECK(op(id, 1, 1, SEM_UNDO) == 0);
		CHECK(op(id, 1, 2, 0) == 0);
		return fails ? 1 : 7;
	}
	/* "removed": sleep until the parent removes the set. */
	errno = 0;
	int r = op(id, 0, -1, 0);
	return r == -1 && errno == EIDRM ? 8 : 1;
}

static pid_t spawn(const char *what)
{
	char *argv[] = {"/t/main", (char *)what, NULL};
	pid_t pid;
	int err = posix_spawn(&pid, "/t/main", NULL, NULL, argv, environ);
	CHECK(err == 0);
	return pid;
}

static int wait_status(pid_t pid)
{
	int status;
	CHECK(waitpid(pid, &status, 0) == pid);
	return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

/* Waits until a process sleeps on semaphore 0. */
static void await_sleeper(int id)
{
	for (int i = 0; i < 2000 && semctl(id, 0, GETNCNT) != 1; i++)
		usleep(1000);
	CHECK(semctl(id, 0, GETNCNT) == 1);
}

int main(int argc, char **argv)
{
	if (argc > 1)
		return child(argv[1]);

	int id = semget(KEY, 2, IPC_CREAT | IPC_EXCL | 0600);
	CHECK(id >= 0);
	FAILS(semget(KEY, 2, IPC_CREAT | IPC_EXCL | 0600), EEXIST);
	FAILS(semget(KEY, 3, 0), EINVAL);

	pid_t pid = spawn("post");
	await_sleeper(id);
	CHECK(op(id, 0, 1, 0) == 0);
	CHECK(wait_status(pid) == 7);
	/* The child's SEM_UNDO increment went away with it; the plain one stays. */
	CHECK(semctl(id, 1, GETVAL) == 2);
	CHECK(semctl(id, 1, GETPID) == pid);
	CHECK(semctl(id, 0, GETVAL) == 0);

	struct timespec ts = {0, 20 * 1000 * 1000};
	struct sembuf take = {0, -1, 0};
	FAILS(semtimedop(id, &take, 1, &ts), EAGAIN);
	FAILS(op(id, 0, -1, IPC_NOWAIT), EAGAIN);
	FAILS(op(id, 2, 1, 0), EFBIG);

	struct semid_ds ds;
	union semun { int val; struct semid_ds *buf; unsigned short *array; } arg = {.buf = &ds};
	CHECK(semctl(id, 0, IPC_STAT, arg) == 0);
	CHECK(ds.sem_nsems == 2 && ds.sem_perm.mode == 0600 && ds.sem_perm.uid == getuid());
	CHECK(ds.sem_otime != 0);

	pid = spawn("removed");
	await_sleeper(id);
	CHECK(semctl(id, 0, IPC_RMID) == 0);
	CHECK(wait_status(pid) == 8);
	FAILS(semctl(id, 0, GETVAL), EINVAL);

	printf(fails ? "FAIL\n" : "PASS\n");
	return fails != 0;
}
