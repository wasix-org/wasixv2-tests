// SIG_IGN discards signals, including fatal ones, and setting SIG_IGN discards a pending signal.
// Signals whose default action is to ignore (SIGCHLD, SIGURG, SIGWINCH) do nothing either.
//#ExpectedStdout: survived term
//#ExpectedStdout: pending before 1 after 0
//#ExpectedStdout: survived default-ignore
//#ExpectedExitCode: 0
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
	signal(SIGTERM, SIG_IGN);
	raise(SIGTERM);
	kill(getpid(), SIGTERM);
	printf("survived term\n");

	sigset_t set, pending;
	sigemptyset(&set);
	sigaddset(&set, SIGINT);
	sigprocmask(SIG_BLOCK, &set, 0);
	raise(SIGINT);
	sigpending(&pending);
	int before = sigismember(&pending, SIGINT);
	signal(SIGINT, SIG_IGN);
	sigpending(&pending);
	printf("pending before %d after %d\n", before, sigismember(&pending, SIGINT));
	sigprocmask(SIG_UNBLOCK, &set, 0);

	raise(SIGCHLD);
	raise(SIGURG);
	raise(SIGWINCH);
	printf("survived default-ignore\n");
	return 0;
}
