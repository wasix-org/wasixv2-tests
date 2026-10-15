// raise() runs the handler before it returns; the signal is blocked while its handler runs
// (no SA_NODEFER) and the mask is restored afterwards. SA_RESETHAND resets the disposition.
//#ExpectedStdout: handler 10 depth 1 blocked-inside 1
//#ExpectedStdout: after raise count=1 blocked-after 0
//#ExpectedStdout: resethand count=2 disposition=SIG_DFL
//#ExpectedExitCode: 0
#include <signal.h>
#include <stdio.h>
#include <string.h>

static volatile int count;
static volatile int depth;

static void handler(int sig)
{
	sigset_t cur;
	depth++;
	count++;
	sigprocmask(SIG_BLOCK, 0, &cur);
	if (count == 1)
		printf("handler %d depth %d blocked-inside %d\n", sig, depth,
			sigismember(&cur, SIGUSR1));
	depth--;
}

int main(void)
{
	struct sigaction sa;
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = handler;
	if (sigaction(SIGUSR1, &sa, 0)) return 1;
	if (raise(SIGUSR1)) return 2;
	sigset_t cur;
	sigprocmask(SIG_BLOCK, 0, &cur);
	printf("after raise count=%d blocked-after %d\n", count, sigismember(&cur, SIGUSR1));

	sa.sa_flags = SA_RESETHAND;
	if (sigaction(SIGUSR2, &sa, 0)) return 3;
	raise(SIGUSR2);
	struct sigaction old;
	sigaction(SIGUSR2, 0, &old);
	printf("resethand count=%d disposition=%s\n", count,
		old.sa_handler == SIG_DFL ? "SIG_DFL" : "other");
	return 0;
}
