// siglongjmp out of a handler returns to the sigsetjmp point, and with savemask restores the
// mask, so the signal is no longer blocked. Works repeatedly.
//#ExpectedStdout: jumped 1 blocked 0
//#ExpectedStdout: jumped 2 blocked 0
//#ExpectedStdout: done
//#ExpectedExitCode: 0
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>

static sigjmp_buf env;
static volatile int jumps;

static void handler(int sig)
{
	jumps++;
	siglongjmp(env, jumps);
}

int main(void)
{
	signal(SIGUSR1, handler);
	for (int i = 0; i < 2; i++) {
		int r = sigsetjmp(env, 1);
		if (r == 0) {
			raise(SIGUSR1);
			printf("not reached\n");
			return 1;
		}
		sigset_t cur;
		sigprocmask(SIG_BLOCK, 0, &cur);
		printf("jumped %d blocked %d\n", r, sigismember(&cur, SIGUSR1));
	}
	printf("done\n");
	return 0;
}
