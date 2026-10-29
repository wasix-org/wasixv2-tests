// Signal-driven I/O on a pipe: O_ASYNC with the process as owner (F_SETOWN) sends SIGIO when
// data arrives at the read end; with F_SETSIG a realtime signal comes instead, saying POLL_IN,
// the descriptor and the band, and the write end's owner hears of the room a read makes
// (POLL_OUT). F_GETOWN_EX reports the owner and its kind, and taking O_ASYNC off ends it.
//#ExpectedStdout: owner 1 ex 1 1
//#ExpectedStdout: sigio 1
//#ExpectedStdout: rt POLL_IN fd 1 band 41
//#ExpectedStdout: rt POLL_OUT fd 1 band 304
//#ExpectedStdout: off 0 0
//#ExpectedExitCode: 0
#define _GNU_SOURCE
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t sigios;
static void on_sigio(int sig) { sigios++; }

static int take(int sig, siginfo_t *si)
{
	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, sig);
	struct timespec now = { 0, 0 };
	return sigtimedwait(&set, si, &now);
}

int main(void)
{
	int p[2];
	if (pipe2(p, O_NONBLOCK))
		return 1;
	signal(SIGIO, on_sigio);
	fcntl(p[0], F_SETOWN, getpid());
	struct f_owner_ex ex;
	fcntl(p[0], F_GETOWN_EX, &ex);
	printf("owner %d ex %d %d\n", fcntl(p[0], F_GETOWN) == getpid(), ex.type == F_OWNER_PID,
	       ex.pid == getpid());
	fcntl(p[0], F_SETFL, fcntl(p[0], F_GETFL) | O_ASYNC);
	write(p[1], "a", 1);
	printf("sigio %d\n", sigios);

	int rt = SIGRTMIN;
	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, rt);
	sigprocmask(SIG_BLOCK, &set, 0);
	fcntl(p[0], F_SETSIG, rt);
	write(p[1], "b", 1);
	siginfo_t si;
	int got = take(rt, &si);
	printf("rt %s fd %d band %lx\n", got == rt && si.si_code == POLL_IN ? "POLL_IN" : "?",
	       si.si_fd == p[0], si.si_band);

	fcntl(p[1], F_SETOWN, getpid());
	fcntl(p[1], F_SETSIG, rt);
	fcntl(p[1], F_SETFL, fcntl(p[1], F_GETFL) | O_ASYNC);
	char buf[8];
	read(p[0], buf, sizeof buf);
	got = take(rt, &si);
	printf("rt %s fd %d band %lx\n", got == rt && si.si_code == POLL_OUT ? "POLL_OUT" : "?",
	       si.si_fd == p[1], si.si_band);

	fcntl(p[0], F_SETFL, fcntl(p[0], F_GETFL) & ~O_ASYNC);
	fcntl(p[1], F_SETFL, fcntl(p[1], F_GETFL) & ~O_ASYNC);
	write(p[1], "c", 1);
	read(p[0], buf, sizeof buf);
	printf("off %d %d\n", take(rt, &si) > 0, (fcntl(p[0], F_GETFL) & O_ASYNC) != 0);
	return 0;
}
