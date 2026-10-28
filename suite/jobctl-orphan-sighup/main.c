// Orphaned process groups (POSIX 2.2.2.52). A process group whose members have no parent in
// another group of their session is orphaned. When a parent's exit orphans a group with a stopped
// process in it, every member gets SIGHUP and then SIGCONT, so the stopped one wakes to its
// handler. In an orphaned group SIGTSTP does nothing, while SIGSTOP still stops.
//#BuildEnv: WASIXCC_WASM_EXCEPTIONS=no
//#ExpectedStdout: middle saw the stop: 1
//#ExpectedStdout: orphaned stopped job: hup then cont
//#ExpectedStdout: new session: tstp ignored=1 stop stopped=1
#define _GNU_SOURCE
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static volatile sig_atomic_t hup, cont;

static void on_hup(int sig)
{
	(void)sig;
	hup = 1;
}

static void on_cont(int sig)
{
	(void)sig;
	cont = 1;
}

static void check(int ok, const char *what)
{
	if (!ok) {
		perror(what);
		exit(1);
	}
}

int main(void)
{
	alarm(20);
	int fds[2];
	check(pipe(fds) == 0, "pipe");
	fflush(stdout);

	// middle: a group of its own, with a child that stops; middle then exits, which orphans
	// the group.
	pid_t middle = fork();
	check(middle >= 0, "fork");
	if (middle == 0) {
		setpgid(0, 0);
		pid_t job = fork();
		if (job == 0) {
			signal(SIGHUP, on_hup);
			signal(SIGCONT, on_cont);
			raise(SIGSTOP);
			// Both handlers ran by the time the job goes on.
			char got[2] = {hup ? 'h' : '-', cont ? 'c' : '-'};
			(void)!write(fds[1], got, 2);
			_exit(0);
		}
		int status;
		int ok = waitpid(job, &status, WUNTRACED) == job && WIFSTOPPED(status);
		(void)!write(fds[1], ok ? "s" : "-", 1);
		_exit(0);
	}
	close(fds[1]);
	int status;
	check(waitpid(middle, &status, 0) == middle, "waitpid");
	char got[8] = {0};
	size_t n = 0;
	while (n < 3) {
		ssize_t r = read(fds[0], got + n, 3 - n);
		if (r <= 0) break;
		n += r;
	}
	printf("middle saw the stop: %d\n", got[0] == 's');
	printf("orphaned stopped job: %s\n",
	       strcmp(got + 1, "hc") == 0 ? "hup then cont" : got + 1);

	// A new session is an orphaned group of its own.
	fflush(stdout);
	pid_t lone = fork();
	check(lone >= 0, "fork");
	if (lone == 0) {
		setsid();
		raise(SIGTSTP);
		_exit(0);
	}
	check(waitpid(lone, &status, WUNTRACED) == lone, "waitpid");
	int ignored = WIFEXITED(status) && WEXITSTATUS(status) == 0;
	lone = fork();
	check(lone >= 0, "fork");
	if (lone == 0) {
		setsid();
		raise(SIGSTOP);
		_exit(0);
	}
	check(waitpid(lone, &status, WUNTRACED) == lone, "waitpid");
	int stopped = WIFSTOPPED(status);
	kill(lone, SIGKILL);
	waitpid(lone, &status, 0);
	printf("new session: tstp ignored=%d stop stopped=%d\n", ignored, stopped);
	return 0;
}
