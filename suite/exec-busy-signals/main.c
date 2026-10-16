// A guest that computes without making syscalls still takes signals, at the preemption points
// of its loops: SIGKILL and SIGTERM end a spinning child, and a SIGALRM handler runs in the
// middle of a busy loop, which then sees its flag.
//#MappedDirectory: .:/t
//#ExpectedStdout: kill: signaled=1 sig=9
//#ExpectedStdout: term: signaled=1 sig=15
//#ExpectedStdout: alarm handled
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

static volatile sig_atomic_t alarmed;

static void on_alarm(int sig)
{
	(void)sig;
	alarmed = 1;
}

static void kill_spinning(char *what, int sig)
{
	char *args[] = {"/t/main", "spin", NULL};
	pid_t pid;
	int status;
	if (posix_spawn(&pid, "/t/main", NULL, NULL, args, environ) != 0) {
		perror("posix_spawn");
		exit(1);
	}
	struct timespec pause = {0, 50 * 1000 * 1000};
	nanosleep(&pause, 0);
	kill(pid, sig);
	if (waitpid(pid, &status, 0) != pid) {
		perror("waitpid");
		exit(1);
	}
	printf("%s: signaled=%d sig=%d\n", what, WIFSIGNALED(status), WTERMSIG(status));
}

int main(int argc, char **argv)
{
	if (argc > 1 && strcmp(argv[1], "spin") == 0) {
		for (volatile unsigned long i = 0;; i++) {
		}
	}
	kill_spinning("kill", SIGKILL);
	kill_spinning("term", SIGTERM);

	signal(SIGALRM, on_alarm);
	struct itimerval timer = {{0, 0}, {0, 50 * 1000}};
	setitimer(ITIMER_REAL, &timer, 0);
	while (!alarmed) {
	}
	printf("alarm handled\n");
	return 0;
}
