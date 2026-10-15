// abort() runs a SIGABRT handler, and when the handler returns the process still dies of
// SIGABRT: 128 + 6.
//#ExpectedStdout: handler 6
//#ExpectedExitCode: 134
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

static void handler(int sig)
{
	printf("handler %d\n", sig);
	fflush(stdout);
}

int main(void)
{
	signal(SIGABRT, handler);
	abort();
}
