// POSIX message queues: create and open by name, attributes and their limits, messages in
// priority order, EMSGSIZE, EAGAIN with O_NONBLOCK, mq_timedreceive's timeout, poll on the
// descriptor, mq_setattr, a receiver woken by another process, and a queue outliving
// mq_unlink while open.
//#MappedDirectory: .:/t
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <poll.h>
#include <spawn.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%d line %d: CHECK(%s) failed (errno %s)\n", getpid(), __LINE__, #c, strerror(errno)); fails++; } } while (0)
#define FAILS(call, e) do { errno = 0; long r_ = (long)(call); if (r_ != -1 || errno != (e)) { fprintf(stderr, "line %d: %s = %ld (%s), want -1 %s\n", __LINE__, #call, r_, strerror(errno), #e); fails++; } } while (0)

static int child(void)
{
	mqd_t q = mq_open("/basic", O_WRONLY);
	CHECK(q != (mqd_t)-1);
	usleep(20000);
	CHECK(mq_send(q, "wake", 4, 3) == 0);
	return fails != 0;
}

int main(int argc, char **argv)
{
	if (argc > 1)
		return child();

	struct mq_attr attr = {.mq_maxmsg = 2, .mq_msgsize = 16};
	mqd_t q = mq_open("/basic", O_CREAT | O_EXCL | O_RDWR, 0600, &attr);
	CHECK(q != (mqd_t)-1);
	FAILS(mq_open("/basic", O_CREAT | O_EXCL | O_RDWR, 0600, &attr), EEXIST);
	FAILS(mq_open("/missing", O_RDWR), ENOENT);
	struct mq_attr big = {.mq_maxmsg = 100000, .mq_msgsize = 16};
	FAILS(mq_open("/big", O_CREAT | O_RDWR, 0600, &big), EINVAL);
	struct mq_attr zero = {.mq_maxmsg = 0, .mq_msgsize = 16};
	FAILS(mq_open("/zero", O_CREAT | O_RDWR, 0600, &zero), EINVAL);
	FAILS(mq_open("/a/b", O_CREAT | O_RDWR, 0600, NULL), EACCES);
	CHECK(fcntl(q, F_GETFD) == FD_CLOEXEC);

	struct mq_attr got;
	CHECK(mq_getattr(q, &got) == 0);
	CHECK(got.mq_maxmsg == 2 && got.mq_msgsize == 16 && got.mq_curmsgs == 0 && got.mq_flags == 0);

	struct pollfd pfd = {q, POLLIN | POLLOUT, 0};
	CHECK(poll(&pfd, 1, 0) == 1 && pfd.revents == POLLOUT);
	CHECK(mq_send(q, "low", 3, 1) == 0);
	CHECK(mq_send(q, "high", 4, 9) == 0);
	CHECK(poll(&pfd, 1, 0) == 1 && pfd.revents == POLLIN);
	FAILS(mq_send(q, "x", 1, 32768), EINVAL);
	FAILS(mq_send(q, "0123456789abcdefg", 17, 0), EMSGSIZE);

	struct mq_attr nonblock = {.mq_flags = O_NONBLOCK};
	CHECK(mq_setattr(q, &nonblock, &got) == 0 && got.mq_flags == 0 && got.mq_curmsgs == 2);
	FAILS(mq_send(q, "full", 4, 0), EAGAIN);

	char buf[16];
	unsigned prio;
	FAILS(mq_receive(q, buf, 8, &prio), EMSGSIZE);
	CHECK(mq_receive(q, buf, sizeof buf, &prio) == 4 && prio == 9 && memcmp(buf, "high", 4) == 0);
	CHECK(mq_receive(q, buf, sizeof buf, &prio) == 3 && prio == 1 && memcmp(buf, "low", 3) == 0);
	FAILS(mq_receive(q, buf, sizeof buf, &prio), EAGAIN);

	struct mq_attr blocking = {.mq_flags = 0};
	CHECK(mq_setattr(q, &blocking, NULL) == 0);
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_nsec += 20 * 1000 * 1000;
	if (ts.tv_nsec >= 1000000000) {
		ts.tv_sec++;
		ts.tv_nsec -= 1000000000;
	}
	FAILS(mq_timedreceive(q, buf, sizeof buf, &prio, &ts), ETIMEDOUT);
	struct timespec bad = {0, 1000000000};
	FAILS(mq_timedreceive(q, buf, sizeof buf, &prio, &bad), EINVAL);

	/* A blocked receiver wakes when another process sends. */
	char *argv2[] = {"/t/main", "child", NULL};
	pid_t pid;
	CHECK(posix_spawn(&pid, "/t/main", NULL, NULL, argv2, environ) == 0);
	CHECK(mq_receive(q, buf, sizeof buf, &prio) == 4 && prio == 3 && memcmp(buf, "wake", 4) == 0);
	int status;
	CHECK(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0);

	/* The name goes at once, the queue with its last descriptor. */
	CHECK(mq_unlink("/basic") == 0);
	FAILS(mq_unlink("/basic"), ENOENT);
	FAILS(mq_open("/basic", O_RDWR), ENOENT);
	CHECK(mq_send(q, "still", 5, 0) == 0);
	CHECK(mq_receive(q, buf, sizeof buf, &prio) == 5);
	CHECK(mq_close(q) == 0);
	FAILS(mq_send(q, "closed", 6, 0), EBADF);
	FAILS(mq_send(0, "stdin", 5, 0), EBADF);

	printf(fails ? "FAIL\n" : "PASS\n");
	return fails != 0;
}
