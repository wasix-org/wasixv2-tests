// An SA_SIGINFO handler gets a siginfo_t and a ucontext: kill() fills si_code SI_USER and the
// sender's pid and uid, sigqueue() SI_QUEUE and the value, and uc_sigmask holds the mask before
// delivery.
//#ExpectedStdout: kill signo 10 code SI_USER pid-ok 1 uid-ok 1 ctx 1 premask-usr2 1
//#ExpectedStdout: queue signo 10 code SI_QUEUE value 42
//#ExpectedExitCode: 0
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>

static int round;

static void handler(int sig, siginfo_t *si, void *ctx)
{
	ucontext_t *uc = ctx;
	if (round == 0)
		printf("kill signo %d code %s pid-ok %d uid-ok %d ctx %d premask-usr2 %d\n",
			si->si_signo, si->si_code == SI_USER ? "SI_USER" : "?",
			si->si_pid == getpid(), si->si_uid == getuid(), uc != 0,
			sigismember(&uc->uc_sigmask, SIGUSR2));
	else
		printf("queue signo %d code %s value %d\n", si->si_signo,
			si->si_code == SI_QUEUE ? "SI_QUEUE" : "?", si->si_value.sival_int);
}

int main(void)
{
	struct sigaction sa;
	memset(&sa, 0, sizeof sa);
	sa.sa_sigaction = handler;
	sa.sa_flags = SA_SIGINFO;
	sigaction(SIGUSR1, &sa, 0);
	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, SIGUSR2);
	sigprocmask(SIG_BLOCK, &set, 0);
	kill(getpid(), SIGUSR1);
	round = 1;
	sigqueue(getpid(), SIGUSR1, (union sigval){ .sival_int = 42 });
	return 0;
}
