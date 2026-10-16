// A handler is called through its own type, whatever SA_SIGINFO says, as native code ignores
// arguments a handler does not declare: a three-argument handler installed without SA_SIGINFO
// gets the signal number, and one- and zero-argument handlers installed with SA_SIGINFO run.
//#ExpectedStdout: three-arg without SA_SIGINFO: sig=10
//#ExpectedStdout: one-arg with SA_SIGINFO: sig=12
//#ExpectedStdout: no-arg with SA_SIGINFO: ran=1
//#ExpectedExitCode: 0
#include <signal.h>
#include <stdio.h>
#include <string.h>

static volatile int got, ran;

static void three(int sig, siginfo_t *si, void *ctx)
{
	got = sig;
}

static void one(int sig)
{
	got = sig;
}

static void none(void)
{
	ran = 1;
}

static void install(int sig, void *handler, int flags)
{
	struct sigaction sa;
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = (void (*)(int))handler;
	sa.sa_flags = flags;
	sigaction(sig, &sa, 0);
}

int main(void)
{
	install(SIGUSR1, (void *)three, 0);
	raise(SIGUSR1);
	printf("three-arg without SA_SIGINFO: sig=%d\n", got);

	install(SIGUSR2, (void *)one, SA_SIGINFO);
	raise(SIGUSR2);
	printf("one-arg with SA_SIGINFO: sig=%d\n", got);

	install(SIGHUP, (void *)none, SA_SIGINFO);
	raise(SIGHUP);
	printf("no-arg with SA_SIGINFO: ran=%d\n", ran);
	return 0;
}
