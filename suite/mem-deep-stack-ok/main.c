// The guard at address 0 sits below the main stack, which wasixcc places first in memory, so it
// takes nothing from the stack a program can use: recursion through 7.5 MiB of an 8 MiB stack
// works.
//#ExpectedStdout: depth=120 sum=7680
#include <stdio.h>

#define FRAME (64 * 1024)

static unsigned recurse(unsigned depth)
{
	volatile char buf[FRAME];
	for (unsigned i = 0; i < FRAME; i += 4096)
		buf[i] = 1;
	unsigned sum = 0;
	for (unsigned i = 0; i < FRAME; i += 1024)
		sum += buf[i - i % 4096];
	return depth == 1 ? sum : sum + recurse(depth - 1);
}

int main(void)
{
	printf("depth=120 sum=%u\n", recurse(120));
	return 0;
}
