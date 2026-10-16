// SysV shared memory between processes started with posix_spawn, handing data over under a
// semaphore: the attachment of each side is synchronised with the segment at shmat, shmdt and
// every semop. Also IPC_STAT's counts, IPC_RMID of an attached segment, and shmat's errors.
//#MappedDirectory: .:/t
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <spawn.h>
#include <stdio.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

#define SHM_KEY 0x5a5a
#define SEM_KEY 0x5a5b

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%d line %d: CHECK(%s) failed (errno %s)\n", getpid(), __LINE__, #c, strerror(errno)); fails++; } } while (0)
#define FAILS(call, e) do { errno = 0; long r_ = (long)(call); if (r_ != -1 || errno != (e)) { fprintf(stderr, "line %d: %s = %ld (%s), want -1 %s\n", __LINE__, #call, r_, strerror(errno), #e); fails++; } } while (0)

static int op(int id, unsigned short num, short delta)
{
	struct sembuf sop = {num, delta, 0};
	return semop(id, &sop, 1);
}

static int child(void)
{
	int shm = shmget(SHM_KEY, 0, 0);
	int sem = semget(SEM_KEY, 0, 0);
	CHECK(shm >= 0 && sem >= 0);
	char *p = shmat(shm, NULL, 0);
	CHECK(p != (void *)-1);
	/* Semaphore 0: the parent's data is there. */
	CHECK(op(sem, 0, -1) == 0);
	CHECK(strcmp(p, "from parent") == 0);
	strcpy(p + 100, "from child");
	/* Semaphore 1: the reply is there. */
	CHECK(op(sem, 1, 1) == 0);
	CHECK(shmdt(p) == 0);
	return fails ? 1 : 0;
}

int main(int argc, char **argv)
{
	if (argc > 1)
		return child();

	int shm = shmget(SHM_KEY, 1000, IPC_CREAT | IPC_EXCL | 0600);
	int sem = semget(SEM_KEY, 2, IPC_CREAT | IPC_EXCL | 0600);
	CHECK(shm >= 0 && sem >= 0);
	FAILS(shmget(SHM_KEY, 2000, 0), EINVAL);
	FAILS(shmget(IPC_PRIVATE, 0, IPC_CREAT | 0600), EINVAL);
	char *p = shmat(shm, NULL, 0);
	CHECK(p != (void *)-1);
	CHECK(p[0] == 0 && p[999] == 0);
	strcpy(p, "from parent");

	char *argv2[] = {"/t/main", "child", NULL};
	pid_t pid;
	CHECK(posix_spawn(&pid, "/t/main", NULL, NULL, argv2, environ) == 0);
	CHECK(op(sem, 0, 1) == 0);
	CHECK(op(sem, 1, -1) == 0);
	CHECK(strcmp(p + 100, "from child") == 0);
	int status;
	CHECK(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0);

	struct shmid_ds ds;
	CHECK(shmctl(shm, IPC_STAT, &ds) == 0);
	CHECK(ds.shm_segsz == 1000 && ds.shm_nattch == 1 && ds.shm_cpid == getpid());
	CHECK(ds.shm_lpid == pid);

	/* A second attachment in this process is a copy too; shmdt and shmat bring it up to date. */
	char *q = shmat(shm, NULL, SHM_RDONLY);
	CHECK(q != (void *)-1 && q != p && strcmp(q, "from parent") == 0);
	CHECK(shmdt(q) == 0);
	FAILS(shmdt(q), EINVAL);
	FAILS(shmat(shm, (void *)1, 0), EINVAL);
	FAILS(shmat(-1, NULL, 0), EINVAL);

	/* Removed while attached: the key no longer finds it, the id still does until detach. */
	CHECK(shmctl(shm, IPC_RMID, NULL) == 0);
	FAILS(shmget(SHM_KEY, 0, 0), ENOENT);
	CHECK(shmctl(shm, IPC_STAT, &ds) == 0 && (ds.shm_perm.mode & SHM_DEST));
	CHECK(shmdt(p) == 0);
	FAILS(shmctl(shm, IPC_STAT, &ds), EINVAL);
	CHECK(semctl(sem, 0, IPC_RMID) == 0);

	printf(fails ? "FAIL\n" : "PASS\n");
	return fails != 0;
}
