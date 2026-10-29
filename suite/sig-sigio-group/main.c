// A process group as the owner of a pipe's read end (F_SETOWN with the negated group id):
// data arriving signals every process of the group, here the parent and its forked child,
// and the owner reads back negated. A process owner signals that process alone.
//#BuildEnv: WASIXCC_WASM_EXCEPTIONS=no
//#ExpectedStdout: getown 1
//#ExpectedStdout: group parent 1 child 1
//#ExpectedStdout: process parent 0 child 1
//#ExpectedExitCode: 0
#define _GNU_SOURCE
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int took(int seconds)
{
	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, SIGIO);
	struct timespec ts = { seconds, 0 };
	return sigtimedwait(&set, 0, &ts) == SIGIO;
}

/// Forks a child that waits for SIGIO and reports it in its exit status; once it waits, makes
/// the owner `owner`, or the child itself for 0, and writes to the pipe. Prints whether the
/// parent and the child got the signal.
static void signal_round(const char *what, int owner, int p[2], int go[2])
{
	pid_t child = fork();
	if (child == 0) {
		write(go[1], "r", 1);
		_exit(took(5));
	}
	char c;
	read(go[0], &c, 1);
	fcntl(p[0], F_SETOWN, owner ? owner : child);
	write(p[1], "x", 1);
	read(p[0], &c, 1);
	int status = 0;
	waitpid(child, &status, 0);
	int parent = took(0);
	printf("%s parent %d child %d\n", what, parent, WIFEXITED(status) && WEXITSTATUS(status));
}

int main(void)
{
	int p[2], go[2];
	if (pipe(p) || pipe(go))
		return 1;
	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, SIGIO);
	sigprocmask(SIG_BLOCK, &set, 0);
	fcntl(p[0], F_SETFL, fcntl(p[0], F_GETFL) | O_ASYNC);

	fcntl(p[0], F_SETOWN, -getpgrp());
	printf("getown %d\n", fcntl(p[0], F_GETOWN) == -getpgrp());
	signal_round("group", -getpgrp(), p, go);
	signal_round("process", 0, p, go);
	return 0;
}
