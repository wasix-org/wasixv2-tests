// How a process ends: exit runs atexit handlers in reverse order and flushes stdio, returning
// from main is exit, only the low byte of the status survives, and _exit skips the handlers.
//#Config: exit
//#Args: exit
//#ExpectedStdout: main
//#ExpectedStdout: atexit 2
//#ExpectedStdout: atexit 1
//#ExpectedExitCode: 42
//#Config: return
//#Args: return
//#ExpectedStdout: main
//#ExpectedStdout: atexit 2
//#ExpectedStdout: atexit 1
//#ExpectedExitCode: 44
//#Config: negative
//#Args: negative
//#ExpectedStdout: main
//#ExpectedStdout: atexit 2
//#ExpectedStdout: atexit 1
//#ExpectedExitCode: 255
//#Config: underscore-exit
//#Args: _exit
//#ExpectedStdout: unbuffered
//#ExpectedExitCode: 7
//#Config: quick-exit
//#Args: quick_exit
//#ExpectedStdout: main
//#ExpectedStdout: at_quick_exit
//#ExpectedExitCode: 9
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void one(void)
{
	printf("atexit 1\n");
}

static void two(void)
{
	printf("atexit 2\n");
}

static void quick(void)
{
	printf("at_quick_exit\n");
	fflush(stdout);
}

int main(int argc, char **argv)
{
	if (argc < 2)
		return 1;
	atexit(one);
	atexit(two);
	at_quick_exit(quick);
	if (strcmp(argv[1], "_exit") == 0) {
		write(1, "unbuffered\n", 11);
		_exit(7);
	}
	/* Fully buffered when stdout is a pipe: only exit's flush writes this. */
	printf("main\n");
	if (strcmp(argv[1], "exit") == 0)
		exit(42);
	if (strcmp(argv[1], "negative") == 0)
		exit(-1);
	if (strcmp(argv[1], "quick_exit") == 0) {
		fflush(stdout);
		quick_exit(9);
	}
	return 300;
}
