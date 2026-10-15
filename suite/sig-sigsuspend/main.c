// sigsuspend waits with a temporary mask, runs the handler under it, returns EINTR, and puts the
// original mask back. The signal is armed with alarm() so it arrives while suspended.
//#ExpectedStdout: handler 14 usr1-blocked-in-handler 0
//#ExpectedStdout: sigsuspend -1 EINTR alrm-blocked-after 1
//#ExpectedExitCode: 0
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

static void handler(int sig)
{
	sigset_t cur;
	sigprocmask(SIG_BLOCK, 0, &cur);
	printf("handler %d usr1-blocked-in-handler %d\n", sig, sigismember(&cur, SIGUSR1));
}

int main(void)
{
	signal(SIGALRM, handler);
	sigset_t block, wait;
	sigemptyset(&block);
	sigaddset(&block, SIGALRM);
	sigaddset(&block, SIGUSR1);
	sigprocmask(SIG_BLOCK, &block, 0);
	alarm(1);
	sigemptyset(&wait);
	int r = sigsuspend(&wait);
	int e = errno;
	sigset_t cur;
	sigprocmask(SIG_BLOCK, 0, &cur);
	printf("sigsuspend %d %s alrm-blocked-after %d\n", r, e == EINTR ? "EINTR" : "?",
		sigismember(&cur, SIGALRM));
	return 0;
}
