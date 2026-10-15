// The smallest end-to-end check: a libc2 program starts, sees its arguments and environment,
// writes to stdout and exits with a status.
//#Args: one two
//#Env: GREETING=hi
//#ExpectedStdout: argc=3 argv[1]=one argv[2]=two GREETING=hi
//#ExpectedExitCode: 3
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
	printf("argc=%d argv[1]=%s argv[2]=%s GREETING=%s\n", argc, argv[1], argv[2],
		getenv("GREETING"));
	return argc;
}
