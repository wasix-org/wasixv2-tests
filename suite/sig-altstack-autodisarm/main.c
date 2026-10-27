// SS_AUTODISARM: the handler runs on the alternate stack, which is disarmed while it runs, so
// sigaltstack reports SS_DISABLE there and lets the handler set a stack; the frame's uc_stack holds
// the stack as set, and the handler's return re-arms it. A handler that siglongjmps out restores
// nothing, so the stack stays disarmed.
//#ExpectedStdout: handler: on-stack=1 flags=disable uc_stack=armed set=0
//#ExpectedStdout: after return: autodisarm=1 same=1
//#ExpectedStdout: after siglongjmp: flags=disable
//#ExpectedExitCode: 0
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *stack, *other;
static size_t size = SIGSTKSZ;
static sigjmp_buf env;
static int jump;

static void handler(int sig, siginfo_t *info, void *ctx)
{
	char local;
	ucontext_t *uc = ctx;
	stack_t cur;
	sigaltstack(0, &cur);
	if (jump)
		siglongjmp(env, 1);
	stack_t set = { .ss_sp = other, .ss_size = size, .ss_flags = SS_AUTODISARM };
	int ret = sigaltstack(&set, 0);
	printf("handler: on-stack=%d flags=%s uc_stack=%s set=%d\n",
		&local >= stack && &local < stack + size,
		cur.ss_flags == SS_DISABLE ? "disable" : "other",
		uc->uc_stack.ss_sp == stack && uc->uc_stack.ss_flags == SS_AUTODISARM ? "armed" : "other",
		ret);
}

int main(void)
{
	stack = malloc(size);
	other = malloc(size);
	stack_t ss = { .ss_sp = stack, .ss_size = size, .ss_flags = SS_AUTODISARM };
	if (sigaltstack(&ss, 0))
		return 1;
	struct sigaction sa;
	memset(&sa, 0, sizeof sa);
	sa.sa_sigaction = handler;
	sa.sa_flags = SA_ONSTACK | SA_SIGINFO;
	sigaction(SIGUSR1, &sa, 0);
	raise(SIGUSR1);
	stack_t cur;
	sigaltstack(0, &cur);
	printf("after return: autodisarm=%d same=%d\n", cur.ss_flags == SS_AUTODISARM,
		cur.ss_sp == stack && cur.ss_size == size);

	jump = 1;
	if (sigsetjmp(env, 1) == 0) {
		raise(SIGUSR1);
		return 2;
	}
	sigaltstack(0, &cur);
	printf("after siglongjmp: flags=%s\n", cur.ss_flags == SS_DISABLE ? "disable" : "other");
	return 0;
}
