// An SA_ONSTACK handler runs on the alternate stack: its locals live inside it, sigaltstack
// reports SS_ONSTACK there and refuses changes with EPERM. Too small a stack is ENOMEM.
//#ExpectedStdout: enomem -1
//#ExpectedStdout: in-stack 1 onstack 1 eperm -1
//#ExpectedStdout: after flags 0
//#ExpectedExitCode: 0
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *stack;
static size_t size = SIGSTKSZ;

static void handler(int sig)
{
	char local;
	stack_t cur, other = { .ss_sp = stack, .ss_size = size };
	sigaltstack(0, &cur);
	int eperm = sigaltstack(&other, 0);
	printf("in-stack %d onstack %d eperm %d\n",
		&local >= stack && &local < stack + size,
		(cur.ss_flags & SS_ONSTACK) != 0, eperm == -1 && errno == EPERM ? -1 : 0);
}

int main(void)
{
	stack = malloc(size);
	stack_t ss = { .ss_sp = stack, .ss_size = 16, .ss_flags = 0 };
	printf("enomem %d\n", sigaltstack(&ss, 0) == -1 && errno == ENOMEM ? -1 : 0);
	ss.ss_size = size;
	if (sigaltstack(&ss, 0)) return 1;
	struct sigaction sa;
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = handler;
	sa.sa_flags = SA_ONSTACK;
	sigaction(SIGUSR1, &sa, 0);
	raise(SIGUSR1);
	stack_t cur;
	sigaltstack(0, &cur);
	printf("after flags %d\n", cur.ss_flags);
	return 0;
}
