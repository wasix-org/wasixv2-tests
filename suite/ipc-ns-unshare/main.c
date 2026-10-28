// unshare(CLONE_NEWIPC): a new IPC namespace starts without SysV message queues, semaphore
// sets, segments or POSIX message queues, so keys and names the old one holds are not found,
// and an object made in it is its own; setns back into the old namespace finds the old objects
// under their old ids. Without CAP_SYS_ADMIN (a uid other than 0), unshare fails with EPERM.
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <unistd.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: CHECK(%s) failed (errno %s)\n", __LINE__, #c, strerror(errno)); fails++; } } while (0)
#define FAILS(call, e) do { errno = 0; long r_ = (long)(call); if (r_ != -1 || errno != (e)) { fprintf(stderr, "line %d: %s = %ld (%s), want -1 %s\n", __LINE__, #call, r_, strerror(errno), #e); fails++; } } while (0)

#define KEY 0x6a9d
#define QUEUE "/ipc-ns-unshare"

static void in_a_new_namespace(void)
{
	int msg = msgget(KEY, IPC_CREAT | 0600);
	int sem = semget(KEY, 1, IPC_CREAT | 0600);
	int shm = shmget(KEY, 4096, IPC_CREAT | 0600);
	mqd_t mq = mq_open(QUEUE, O_RDWR | O_CREAT, 0600, NULL);
	CHECK(msg >= 0 && sem >= 0 && shm >= 0 && mq != (mqd_t)-1);
	int old = open("/proc/self/ns/ipc", O_RDONLY);
	CHECK(old >= 0);

	CHECK(unshare(CLONE_NEWIPC) == 0);

	FAILS(msgget(KEY, 0), ENOENT);
	FAILS(semget(KEY, 0, 0), ENOENT);
	FAILS(shmget(KEY, 0, 0), ENOENT);
	FAILS(mq_open(QUEUE, O_RDWR), ENOENT);
	int mine = msgget(KEY, IPC_CREAT | IPC_EXCL | 0600);
	CHECK(mine >= 0);
	CHECK(msgctl(mine, IPC_RMID, NULL) == 0);

	CHECK(setns(old, CLONE_NEWIPC) == 0);

	CHECK(msgget(KEY, 0) == msg);
	CHECK(semget(KEY, 0, 0) == sem);
	CHECK(shmget(KEY, 0, 0) == shm);
	mqd_t again = mq_open(QUEUE, O_RDWR);
	CHECK(again != (mqd_t)-1);
	CHECK(msgctl(msg, IPC_RMID, NULL) == 0);
	CHECK(semctl(sem, 0, IPC_RMID) == 0);
	CHECK(shmctl(shm, IPC_RMID, NULL) == 0);
	CHECK(mq_unlink(QUEUE) == 0);
}

int main(void)
{
	if (geteuid() != 0)
		FAILS(unshare(CLONE_NEWIPC), EPERM);
	else
		in_a_new_namespace();
	if (fails)
		return 1;
	puts("PASS");
	return 0;
}
