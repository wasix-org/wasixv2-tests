// A signal whose default action terminates kills the process; `wasmer run` reports it as the
// shell does, 128 + signal (SIGTERM = 15). Nothing after the kill runs.
//#ExpectedStdout: before
//#ExpectedExitCode: 143
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
	printf("before\n");
	fflush(stdout);
	kill(getpid(), SIGTERM);
	printf("after\n");
	return 0;
}
