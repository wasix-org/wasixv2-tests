// sigtimedwait takes a blocked pending signal without running its handler, times out with
// EAGAIN, and realtime signals queue with their values in order.
//#ExpectedStdout: got 10 code SI_TKILL handled 0
//#ExpectedStdout: timeout -1 EAGAIN
//#ExpectedStdout: rt 1 2 3
//#ExpectedExitCode: 0
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

static volatile int handled;
static void handler(int sig) { handled++; }

int main(void)
{
	signal(SIGUSR1, handler);
	int rt = SIGRTMIN + 1;
	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, SIGUSR1);
	sigaddset(&set, rt);
	sigprocmask(SIG_BLOCK, &set, 0);

	raise(SIGUSR1);
	siginfo_t si;
	struct timespec ts = { 0, 0 };
	int sig = sigtimedwait(&set, &si, &ts);
	printf("got %d code %s handled %d\n", sig, si.si_code == SI_TKILL ? "SI_TKILL" : "?",
		handled);

	ts.tv_nsec = 50 * 1000 * 1000;
	int r = sigtimedwait(&set, &si, &ts);
	printf("timeout %d %s\n", r, errno == EAGAIN ? "EAGAIN" : "?");

	for (int i = 1; i <= 3; i++)
		sigqueue(getpid(), rt, (union sigval){ .sival_int = i });
	int v[3];
	for (int i = 0; i < 3; i++) {
		if (sigwaitinfo(&set, &si) != rt) return 1;
		v[i] = si.si_value.sival_int;
	}
	printf("rt %d %d %d\n", v[0], v[1], v[2]);
	return 0;
}
