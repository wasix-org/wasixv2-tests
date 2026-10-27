// RLIMIT_SIGPENDING bounds the signals queued for a user: past it, sigqueue of a realtime signal
// fails with EAGAIN, while kill still makes the signal pending, without its siginfo, which then
// reads as SI_USER from pid 0. Taking queued signals makes room again.
//#ExpectedStdout: queued=2 sigqueue=EAGAIN kill=0
//#ExpectedStdout: killed: code=SI_USER pid=0
//#ExpectedStdout: taken: 1 2
//#ExpectedStdout: again=0
//#ExpectedExitCode: 0
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <sys/resource.h>
#include <unistd.h>

static int take(int sig, siginfo_t *info)
{
	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, sig);
	struct timespec zero = { 0, 0 };
	return sigtimedwait(&set, info, &zero);
}

int main(void)
{
	struct rlimit rl = { 2, 2 };
	if (setrlimit(RLIMIT_SIGPENDING, &rl))
		return 1;
	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, SIGRTMIN);
	sigaddset(&set, SIGRTMIN + 1);
	sigprocmask(SIG_BLOCK, &set, 0);
	int queued = 0;
	for (int i = 1; i <= 2; i++)
		queued += sigqueue(getpid(), SIGRTMIN + 1, (union sigval){ .sival_int = i }) == 0;
	int full = sigqueue(getpid(), SIGRTMIN, (union sigval){ .sival_int = 3 });
	int full_errno = errno;
	int killed = kill(getpid(), SIGRTMIN);
	printf("queued=%d sigqueue=%s kill=%d\n", queued,
		full == -1 && full_errno == EAGAIN ? "EAGAIN" : "ok", killed);

	siginfo_t info;
	if (take(SIGRTMIN, &info) != SIGRTMIN)
		return 2;
	printf("killed: code=%s pid=%d\n", info.si_code == SI_USER ? "SI_USER" : "other",
		(int)info.si_pid);
	take(SIGRTMIN + 1, &info);
	int first = info.si_value.sival_int;
	take(SIGRTMIN + 1, &info);
	printf("taken: %d %d\n", first, info.si_value.sival_int);
	printf("again=%d\n", sigqueue(getpid(), SIGRTMIN, (union sigval){ .sival_int = 4 }));
	return 0;
}
