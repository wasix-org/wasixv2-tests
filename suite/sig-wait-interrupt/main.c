// Linux kernel/signal.c do_sigtimedwait returns EINTR for an unrelated caught signal.
// sigwaitinfo has the same behavior; POSIX sigwait must hide EINTR and keep waiting.
//#BuildEnv: WASIXCC_WASM_EXCEPTIONS=no
//#ExpectedStdout: sigtimedwait EINTR
//#ExpectedStdout: sigwaitinfo EINTR
//#ExpectedStdout: sigwait retries EINTR
//#ExpectedStdout: sigwait returns error number
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t handled;
static void handler(int sig) { (void)sig; handled++; }
static void watchdog(int sig) { (void)sig; _exit(3); }

static pid_t sender(int final)
{
	pid_t parent = getpid();
	pid_t child = fork();
	if (!child) {
		usleep(50000);
		if (kill(parent, SIGUSR1)) _exit(4);
		if (final) {
			usleep(50000);
			if (kill(parent, SIGUSR2)) _exit(5);
		}
		_exit(0);
	}
	return child;
}

static int reap(pid_t child)
{
	int status;
	return child > 0 && waitpid(child, &status, 0) == child &&
		WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

int main(void)
{
	struct sigaction action = { .sa_handler = handler };
	sigemptyset(&action.sa_mask);
	if (sigaction(SIGUSR1, &action, NULL)) return 1;
	action.sa_handler = watchdog;
	if (sigaction(SIGALRM, &action, NULL)) return 1;
	sigset_t wanted;
	sigemptyset(&wanted);
	sigaddset(&wanted, SIGUSR2);
	if (sigprocmask(SIG_BLOCK, &wanted, NULL)) return 1;

	for (int info = 0; info < 2; info++) {
		pid_t child = sender(0);
		if (child < 0) return 1;
		alarm(2);
		struct timespec timeout = { .tv_nsec = 200000000 };
		errno = 0;
		int ret = info ? sigwaitinfo(&wanted, NULL) : sigtimedwait(&wanted, NULL, &timeout);
		int error = errno;
		alarm(0);
		if (!reap(child) || ret != -1 || error != EINTR || handled != info + 1) {
			printf("wait %d: ret %d errno %d handled %d\n", info, ret, error, handled);
			return 1;
		}
		puts(info ? "sigwaitinfo EINTR" : "sigtimedwait EINTR");
	}
	pid_t child = sender(1);
	if (child < 0) return 1;
	alarm(2);
	int sig = 0;
	int ret = sigwait(&wanted, &sig);
	alarm(0);
	if (!reap(child) || ret || sig != SIGUSR2 || handled != 3) return 1;
	puts("sigwait retries EINTR");
	if (sigwait((const sigset_t *)(uintptr_t)UINTPTR_MAX, &sig) != EFAULT) return 1;
	puts("sigwait returns error number");
	puts("PASS");
	return 0;
}
