// A blocked signal stays pending, shows in sigpending, standard signals do not queue twice,
// and unblocking delivers it before sigprocmask returns. SIGKILL cannot be blocked.
//#ExpectedStdout: pending 1 handled 0
//#ExpectedStdout: handled 1
//#ExpectedStdout: kill-blocked 0
//#ExpectedStdout: einval -1
//#ExpectedExitCode: 0
#include <errno.h>
#include <signal.h>
#include <stdio.h>

static volatile int handled;

static void handler(int sig)
{
	handled++;
}

int main(void)
{
	signal(SIGUSR1, handler);
	sigset_t set, pending, old;
	sigemptyset(&set);
	sigaddset(&set, SIGUSR1);
	sigprocmask(SIG_BLOCK, &set, &old);
	raise(SIGUSR1);
	raise(SIGUSR1);
	sigpending(&pending);
	printf("pending %d handled %d\n", sigismember(&pending, SIGUSR1), handled);
	sigprocmask(SIG_SETMASK, &old, 0);
	printf("handled %d\n", handled);

	sigset_t all, cur;
	sigfillset(&all);
	sigprocmask(SIG_SETMASK, &all, 0);
	sigprocmask(SIG_SETMASK, 0, &cur);
	printf("kill-blocked %d\n", sigismember(&cur, SIGKILL));
	sigprocmask(SIG_SETMASK, &old, 0);

	printf("einval %d\n", sigprocmask(99, &set, 0));
	return errno == EINVAL ? 0 : 1;
}
