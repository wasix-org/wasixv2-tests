// PR_SET_PDEATHSIG: a process hears of its parent's exit with the signal it asked for, sent as
// kill would from the parent, after it went to its new parent.
//#MappedDirectory: .:/t
//#ExpectedStdout: child: signal=SIGUSR1 code=SI_USER from-parent=1 reparented=1
//#ExpectedStdout: middle exited=0 child exited=0
//#ExpectedExitCode: 0
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

static pid_t spawn(char *mode, char *arg)
{
	char *args[] = {"/t/main", mode, arg, NULL};
	pid_t pid;
	int err = posix_spawn(&pid, "/t/main", NULL, NULL, args, environ);
	if (err) {
		printf("posix_spawn: %s\n", strerror(err));
		exit(1);
	}
	return pid;
}

int main(int argc, char **argv)
{
	if (argc > 2 && strcmp(argv[1], "child") == 0) {
		// argv[2] is the descriptor that tells the parent it may exit.
		sigset_t set;
		sigemptyset(&set);
		sigaddset(&set, SIGUSR1);
		sigprocmask(SIG_BLOCK, &set, 0);
		pid_t parent = getppid();
		prctl(PR_SET_PDEATHSIG, SIGUSR1);
		int ready = atoi(argv[2]);
		write(ready, "r", 1);
		close(ready);
		siginfo_t info;
		int sig = sigwaitinfo(&set, &info);
		printf("child: signal=%s code=%s from-parent=%d reparented=%d\n",
			sig == SIGUSR1 ? "SIGUSR1" : "other", info.si_code == SI_USER ? "SI_USER" : "other",
			info.si_pid == parent, getppid() != parent);
		return 0;
	}
	if (argc > 1 && strcmp(argv[1], "middle") == 0) {
		int fds[2];
		if (pipe(fds))
			return 1;
		char fd[16];
		snprintf(fd, sizeof fd, "%d", fds[1]);
		spawn("child", fd);
		close(fds[1]);
		char c;
		if (read(fds[0], &c, 1) != 1)
			return 1;
		return 0;
	}
	// The child goes to us when the middle process exits.
	prctl(PR_SET_CHILD_SUBREAPER, 1);
	fflush(stdout);
	int status;
	pid_t middle = spawn("middle", "");
	waitpid(middle, &status, 0);
	int middle_status = WEXITSTATUS(status);
	if (waitpid(-1, &status, 0) < 0)
		return 1;
	printf("middle exited=%d child exited=%d\n", middle_status, WEXITSTATUS(status));
	return 0;
}
