// pidfd_send_signal takes an open /proc/<pid> directory where it wants a pidfd (kernel/signal.c:
// pidfd_to_pid falls back to tgid_pidfd_to_pid), as procps' killall uses it: signal 0 probes the
// process, and a signal reaches it. A thread's directory, another /proc file and an O_PATH
// descriptor fail with EBADF, and waitid(P_PIDFD), which wants a real pidfd, takes none of them.
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: CHECK(%s) failed (errno %s)\n", __LINE__, #c, strerror(errno)); fails++; } } while (0)
#define FAILS(call, e) do { errno = 0; long r_ = (long)(call); if (r_ != -1 || errno != (e)) { fprintf(stderr, "line %d: %s = %ld (%s), want -1 %s\n", __LINE__, #call, r_, strerror(errno), #e); fails++; } } while (0)

static volatile sig_atomic_t got;

static void on_signal(int sig)
{
	got = sig;
}

static long send_signal(int fd, int sig)
{
	return syscall(SYS_pidfd_send_signal, fd, sig, NULL, 0);
}

static int open_proc(const char *suffix, int flags)
{
	char path[64];
	snprintf(path, sizeof path, "/proc/%d%s", (int)getpid(), suffix);
	return open(path, flags);
}

int main(void)
{
	int dir = open_proc("", O_RDONLY | O_DIRECTORY);
	CHECK(dir >= 0);
	CHECK(send_signal(dir, 0) == 0);
	CHECK(signal(SIGUSR1, on_signal) != SIG_ERR);
	CHECK(send_signal(dir, SIGUSR1) == 0);
	CHECK(got == SIGUSR1);

	char task[32];
	snprintf(task, sizeof task, "/task/%d", (int)getpid());
	int thread = open_proc(task, O_RDONLY | O_DIRECTORY);
	int stat = open_proc("/stat", O_RDONLY);
	int location = open_proc("", O_PATH);
	CHECK(thread >= 0 && stat >= 0 && location >= 0);
	FAILS(send_signal(thread, 0), EBADF);
	FAILS(send_signal(stat, 0), EBADF);
	FAILS(send_signal(location, 0), EBADF);
	siginfo_t si;
	FAILS(waitid(P_PIDFD, dir, &si, WEXITED), EBADF);

	if (fails)
		return 1;
	puts("PASS");
	return 0;
}
