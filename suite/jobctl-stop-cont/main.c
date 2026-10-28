// Job control without a terminal. SIGTSTP stops a child in a process group of its own (not
// orphaned: its parent is in another group of the session): the parent gets SIGCHLD with
// CLD_STOPPED, waitpid(WUNTRACED) reports the stop once, /proc shows the child as T, and the
// child's blocked read waits until SIGCONT, after which it goes on with no EINTR. The parent then
// gets CLD_CONTINUED, and waitpid(WCONTINUED) reports it once. A stopped child keeps SIGTERM
// pending until SIGCONT, and dies of SIGKILL at once. SA_NOCLDSTOP keeps SIGCHLD away, not
// waitpid.
//#BuildEnv: WASIXCC_WASM_EXCEPTIONS=no
//#ExpectedStdout: stopped: wait=1 sig=20 sigchld=5/20 again=0 state=T
//#ExpectedStdout: while stopped: reply=0
//#ExpectedStdout: continued: wait=1 sigchld=6/18 again=0 reply=y
//#ExpectedStdout: sigterm while stopped: alive=1 then signaled=15
//#ExpectedStdout: sigkill while stopped: signaled=9
//#ExpectedStdout: SA_NOCLDSTOP: wait=1 sigchld=0
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static void check(int ok, const char *what)
{
	if (!ok) {
		perror(what);
		exit(1);
	}
}

// The child: its own process group, then echo 'x' as 'y' until the pipe closes.
static pid_t child(int *down, int *up)
{
	int to_child[2], to_parent[2];
	check(pipe(to_child) == 0 && pipe(to_parent) == 0, "pipe");
	fflush(stdout);
	pid_t pid = fork();
	check(pid >= 0, "fork");
	if (pid == 0) {
		close(to_child[1]);
		close(to_parent[0]);
		setpgid(0, 0);
		check(write(to_parent[1], "r", 1) == 1, "write");
		char c;
		for (;;) {
			ssize_t n = read(to_child[0], &c, 1);
			if (n < 0) _exit(errno == EINTR ? 20 : 21);
			if (n == 0) _exit(7);
			check(write(to_parent[1], "y", 1) == 1, "write");
		}
	}
	close(to_child[0]);
	close(to_parent[1]);
	char c;
	check(read(to_parent[0], &c, 1) == 1, "ready");
	// Its group is set before it says it is ready.
	*down = to_child[1];
	*up = to_parent[0];
	return pid;
}

// The SIGCHLD pending for the parent within 1 s: "code/status", or 0.
static void sigchld(char *out, size_t len, int wait_ms)
{
	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, SIGCHLD);
	struct timespec ts = {wait_ms / 1000, (wait_ms % 1000) * 1000000L};
	siginfo_t info;
	if (sigtimedwait(&set, &info, &ts) == SIGCHLD)
		snprintf(out, len, "%d/%d", info.si_code, info.si_status);
	else
		snprintf(out, len, "0");
}

static char reply(int up, int wait_ms)
{
	struct pollfd p = {up, POLLIN, 0};
	char c = '0';
	if (poll(&p, 1, wait_ms) == 1) check(read(up, &c, 1) == 1, "read reply");
	return c;
}

static char state(pid_t pid)
{
	char path[64], line[512];
	snprintf(path, sizeof path, "/proc/%d/stat", pid);
	FILE *f = fopen(path, "r");
	if (!f) return '?';
	char *ok = fgets(line, sizeof line, f);
	fclose(f);
	char *paren = ok ? strrchr(line, ')') : NULL;
	return paren ? paren[2] : '?';
}

int main(void)
{
	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, SIGCHLD);
	sigprocmask(SIG_BLOCK, &set, NULL);
	char info[32];
	int down, up, status;

	pid_t pid = child(&down, &up);
	check(kill(pid, SIGTSTP) == 0, "kill");
	check(waitpid(pid, &status, WUNTRACED) == pid, "waitpid");
	int stopped = WIFSTOPPED(status);
	int sig = WSTOPSIG(status);
	sigchld(info, sizeof info, 1000);
	int again = waitpid(pid, &status, WUNTRACED | WNOHANG);
	printf("stopped: wait=%d sig=%d sigchld=%s again=%d state=%c\n", stopped, sig, info, again,
	       state(pid));

	check(write(down, "x", 1) == 1, "write");
	printf("while stopped: reply=%c\n", reply(up, 200));

	check(kill(pid, SIGCONT) == 0, "kill");
	check(waitpid(pid, &status, WCONTINUED) == pid, "waitpid");
	int continued = WIFCONTINUED(status);
	sigchld(info, sizeof info, 1000);
	again = waitpid(pid, &status, WCONTINUED | WNOHANG);
	printf("continued: wait=%d sigchld=%s again=%d reply=%c\n", continued, info, again,
	       reply(up, 5000));

	// SIGTERM waits for SIGCONT.
	check(kill(pid, SIGSTOP) == 0, "kill");
	check(waitpid(pid, &status, WUNTRACED) == pid && WIFSTOPPED(status), "stop");
	check(kill(pid, SIGTERM) == 0, "kill");
	struct timespec pause = {0, 200 * 1000000L};
	nanosleep(&pause, NULL);
	int alive = waitpid(pid, &status, WNOHANG) == 0;
	check(kill(pid, SIGCONT) == 0, "kill");
	check(waitpid(pid, &status, 0) == pid, "waitpid");
	printf("sigterm while stopped: alive=%d then signaled=%d\n", alive,
	       WIFSIGNALED(status) ? WTERMSIG(status) : -1);
	close(down);
	close(up);
	while (sigtimedwait(&set, NULL, &(struct timespec){0, 0}) > 0) {}

	pid = child(&down, &up);
	check(kill(pid, SIGSTOP) == 0, "kill");
	check(waitpid(pid, &status, WUNTRACED) == pid && WIFSTOPPED(status), "stop");
	check(kill(pid, SIGKILL) == 0, "kill");
	check(waitpid(pid, &status, 0) == pid, "waitpid");
	printf("sigkill while stopped: signaled=%d\n", WIFSIGNALED(status) ? WTERMSIG(status) : -1);
	close(down);
	close(up);
	while (sigtimedwait(&set, NULL, &(struct timespec){0, 0}) > 0) {}

	struct sigaction sa = {0};
	sa.sa_handler = SIG_DFL;
	sa.sa_flags = SA_NOCLDSTOP;
	sigaction(SIGCHLD, &sa, NULL);
	pid = child(&down, &up);
	check(kill(pid, SIGSTOP) == 0, "kill");
	check(waitpid(pid, &status, WUNTRACED) == pid, "waitpid");
	stopped = WIFSTOPPED(status);
	sigchld(info, sizeof info, 200);
	printf("SA_NOCLDSTOP: wait=%d sigchld=%s\n", stopped, info);
	kill(pid, SIGKILL);
	waitpid(pid, &status, 0);
	return 0;
}
