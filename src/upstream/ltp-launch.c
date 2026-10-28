/*
 * ltp-launch COMMAND [ARG]...: runs an LTP test the way LTP's own runners do, as a child in
 * this process's session with a process group of its own (ltp-pan's and the test library's
 * fork_testrun() setpgid(0, 0)), and exits as it did: with its exit status, or killed by its
 * signal. COMMAND is looked up in PATH.
 *
 * The runner starts this as the program of `wasmer run`, which leads a session of its own, as
 * a process a service manager starts does. On wasixv2 the test library runs the test in its own
 * process, so without this the test would lead that session: a terminal it opens without
 * O_NOCTTY would become its controlling terminal, and that terminal's hangup would send it
 * SIGHUP, which never happens under runltp or kirk.
 */
#include <errno.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>

extern char **environ;

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: ltp-launch COMMAND [ARG]...\n");
		return 2;
	}
	posix_spawnattr_t attr;
	posix_spawnattr_init(&attr);
	posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
	posix_spawnattr_setpgroup(&attr, 0);
	pid_t pid;
	int err = posix_spawnp(&pid, argv[1], NULL, &attr, argv + 1, environ);
	if (err) {
		fprintf(stderr, "ltp-launch: %s: %s\n", argv[1], strerror(err));
		return 127;
	}
	int status;
	while (waitpid(pid, &status, 0) < 0) {
		if (errno != EINTR) {
			perror("ltp-launch: waitpid");
			return 126;
		}
	}
	if (WIFEXITED(status)) return WEXITSTATUS(status);
	/* Die of the same signal, so the runner sees what it would have seen of the test. */
	int sig = WTERMSIG(status);
	signal(sig, SIG_DFL);
	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, sig);
	sigprocmask(SIG_UNBLOCK, &set, NULL);
	raise(sig);
	return 128 + sig;
}
