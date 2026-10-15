// A child's exit sends SIGCHLD to its parent with si_pid, si_status and CLD_EXITED, which a
// SA_SIGINFO handler and sigwaitinfo both see. With SIGCHLD ignored, or SA_NOCLDWAIT set,
// children are reaped as they exit: wait blocks until they are gone, then fails with ECHILD.
//#MappedDirectory: .:/t
//#ExpectedStdout: sigwaitinfo: sig=1 pid=1 status=3 code=1
//#ExpectedStdout: handler: pid=1 status=6
//#ExpectedStdout: SIG_IGN: wait=ECHILD
//#ExpectedStdout: SA_NOCLDWAIT: wait=ECHILD
#include <errno.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

static pid_t spawn(char *code)
{
	char *args[] = {"/t/main", "exit", code, NULL};
	pid_t pid;
	int err = posix_spawn(&pid, "/t/main", NULL, NULL, args, environ);
	if (err) {
		printf("posix_spawn: %s\n", strerror(err));
		exit(1);
	}
	return pid;
}

static volatile sig_atomic_t got_pid, got_status;

static void on_chld(int sig, siginfo_t *si, void *uc)
{
	(void)sig;
	(void)uc;
	got_pid = si->si_pid;
	got_status = si->si_status;
}

int main(int argc, char **argv)
{
	if (argc > 2 && strcmp(argv[1], "exit") == 0)
		return atoi(argv[2]);

	sigset_t chld, old;
	sigemptyset(&chld);
	sigaddset(&chld, SIGCHLD);
	sigprocmask(SIG_BLOCK, &chld, &old);
	pid_t pid = spawn("3");
	siginfo_t si;
	int sig = sigwaitinfo(&chld, &si);
	printf("sigwaitinfo: sig=%d pid=%d status=%d code=%d\n", sig == SIGCHLD, si.si_pid == pid,
		si.si_status, si.si_code == CLD_EXITED);
	waitpid(pid, NULL, 0);

	struct sigaction sa = {0};
	sa.sa_sigaction = on_chld;
	sa.sa_flags = SA_SIGINFO;
	sigaction(SIGCHLD, &sa, NULL);
	pid = spawn("6");
	while (!got_pid)
		sigsuspend(&old);
	printf("handler: pid=%d status=%d\n", got_pid == pid, got_status);
	waitpid(pid, NULL, 0);
	sigprocmask(SIG_SETMASK, &old, NULL);

	signal(SIGCHLD, SIG_IGN);
	spawn("0");
	spawn("1");
	errno = 0;
	waitpid(-1, NULL, 0);
	printf("SIG_IGN: wait=%s\n", errno == ECHILD ? "ECHILD" : strerror(errno));

	sa.sa_handler = SIG_DFL;
	sa.sa_flags = SA_NOCLDWAIT;
	sigaction(SIGCHLD, &sa, NULL);
	spawn("0");
	errno = 0;
	waitpid(-1, NULL, 0);
	printf("SA_NOCLDWAIT: wait=%s\n", errno == ECHILD ? "ECHILD" : strerror(errno));
	return 0;
}
