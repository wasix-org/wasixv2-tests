// SysV message queues: messages by type (first, exact, MSG_EXCEPT, lowest up to a bound),
// E2BIG and MSG_NOERROR truncation, ENOMSG and EAGAIN without waiting, a receiver blocked
// until a process started with posix_spawn sends, IPC_STAT's counters, IPC_SET of the byte
// limit, and IPC_RMID.
//#MappedDirectory: .:/t
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <spawn.h>
#include <stdio.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

#define KEY 0x4d51

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%d line %d: CHECK(%s) failed (errno %s)\n", getpid(), __LINE__, #c, strerror(errno)); fails++; } } while (0)
#define FAILS(call, e) do { errno = 0; long r_ = (long)(call); if (r_ != -1 || errno != (e)) { fprintf(stderr, "line %d: %s = %ld (%s), want -1 %s\n", __LINE__, #call, r_, strerror(errno), #e); fails++; } } while (0)

struct msg {
	long mtype;
	char mtext[64];
};

static int send(int id, long type, const char *text, int flags)
{
	struct msg m = {type, ""};
	strcpy(m.mtext, text);
	return msgsnd(id, &m, strlen(text), flags);
}

static int child(void)
{
	int id = msgget(KEY, 0);
	CHECK(id >= 0);
	usleep(20000);
	CHECK(send(id, 9, "late", 0) == 0);
	return fails != 0;
}

int main(int argc, char **argv)
{
	if (argc > 1)
		return child();

	int id = msgget(KEY, IPC_CREAT | 0600);
	CHECK(id >= 0);
	CHECK(msgget(KEY, 0) == id);
	struct msg m;

	CHECK(send(id, 3, "three", 0) == 0);
	CHECK(send(id, 1, "one", 0) == 0);
	CHECK(send(id, 2, "two", 0) == 0);
	CHECK(send(id, 5, "five", 0) == 0);
	FAILS(send(id, 0, "zero", 0), EINVAL);

	FAILS(msgrcv(id, &m, 2, 3, 0), E2BIG);
	CHECK(msgrcv(id, &m, 2, 3, MSG_NOERROR) == 2 && m.mtype == 3 && memcmp(m.mtext, "th", 2) == 0);
	CHECK(msgrcv(id, &m, sizeof m.mtext, 1, MSG_EXCEPT) == 3 && m.mtype == 2);
	CHECK(msgrcv(id, &m, sizeof m.mtext, -4, 0) == 3 && m.mtype == 1);
	FAILS(msgrcv(id, &m, sizeof m.mtext, 4, IPC_NOWAIT), ENOMSG);

	struct msqid_ds ds;
	CHECK(msgctl(id, IPC_STAT, &ds) == 0);
	CHECK(ds.msg_qnum == 1 && ds.msg_cbytes == 4 && ds.msg_lspid == getpid());
	CHECK(ds.msg_qbytes == 16384);
	CHECK(msgrcv(id, &m, sizeof m.mtext, 0, 0) == 4 && m.mtype == 5);

	/* A full queue: EAGAIN without waiting. */
	ds.msg_qbytes = 8;
	CHECK(msgctl(id, IPC_SET, &ds) == 0);
	CHECK(send(id, 1, "12345678", 0) == 0);
	FAILS(send(id, 1, "x", IPC_NOWAIT), EAGAIN);
	CHECK(msgrcv(id, &m, sizeof m.mtext, 0, 0) == 8);
	ds.msg_qbytes = 16384;
	CHECK(msgctl(id, IPC_SET, &ds) == 0);

	/* A receiver waits for a sender in another process. */
	char *argv2[] = {"/t/main", "child", NULL};
	pid_t pid;
	CHECK(posix_spawn(&pid, "/t/main", NULL, NULL, argv2, environ) == 0);
	CHECK(msgrcv(id, &m, sizeof m.mtext, 9, 0) == 4 && memcmp(m.mtext, "late", 4) == 0);
	CHECK(msgctl(id, IPC_STAT, &ds) == 0 && ds.msg_lspid == pid && ds.msg_lrpid == getpid());
	int status;
	CHECK(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0);

	CHECK(msgctl(id, IPC_RMID, NULL) == 0);
	FAILS(send(id, 1, "gone", 0), EINVAL);
	FAILS(msgget(KEY, 0), ENOENT);

	printf(fails ? "FAIL\n" : "PASS\n");
	return fails != 0;
}
