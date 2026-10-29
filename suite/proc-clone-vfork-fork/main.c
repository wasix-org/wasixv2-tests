// clone(fn, stack, CLONE_VFORK | SIGCHLD) without CLONE_VM in the asyncify build configuration:
// a fork whose parent's clone returns only once the child is done with its copy of the memory,
// as it exits or its execve replaces the memory. The child exits only after it wrote to a pipe,
// so the parent finds the byte there at once; an exec'd child waits for a byte the parent
// writes once its clone returned, and fails if none comes.
//#BuildEnv: WASIXCC_WASM_EXCEPTIONS=no
//#MappedDirectory: .:/t
//#ExpectedStdout: exit: waited=1 status=5 global=100
//#ExpectedStdout: exec: released=1 status=0
#define _GNU_SOURCE
#include <fcntl.h>
#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef SELF
#define SELF "/t/main"
#endif

#define STACK_SIZE 65536

static char stack[STACK_SIZE] __attribute__((aligned(16)));
static void *const top = stack + STACK_SIZE;

static int global = 100;
static int pipe_fds[2];

/// Reaps `pid`; its exit status, or -1.
static int reap(pid_t pid)
{
	int status;
	if (pid <= 0 || waitpid(pid, &status, 0) != pid || !WIFEXITED(status))
		return -1;
	return WEXITSTATUS(status);
}

static int write_then_exit(void *arg)
{
	(void)arg;
	global = 200;
	usleep(200 * 1000);
	return write(pipe_fds[1], "x", 1) == 1 ? 5 : 1;
}

static int exec_reader(void *arg)
{
	(void)arg;
	char fd[16];
	snprintf(fd, sizeof fd, "%d", pipe_fds[0]);
	char *args[] = {SELF, "reader", fd, NULL};
	execv(SELF, args);
	_exit(127);
}

int main(int argc, char **argv)
{
	// The exec'd child: 0 once the parent's byte arrived, 1 if it does not within 10 s.
	if (argc > 2 && strcmp(argv[1], "reader") == 0) {
		struct pollfd p = {.fd = atoi(argv[2]), .events = POLLIN};
		char c;
		return poll(&p, 1, 10 * 1000) == 1 && read(p.fd, &c, 1) == 1 ? 0 : 1;
	}

	if (pipe(pipe_fds) < 0 || fcntl(pipe_fds[0], F_SETFL, O_NONBLOCK) < 0) {
		perror("pipe");
		return 1;
	}
	fflush(stdout);
	pid_t pid = clone(write_then_exit, top, CLONE_VFORK | SIGCHLD, NULL);
	char c;
	int waited = read(pipe_fds[0], &c, 1) == 1;
	int status = reap(pid);
	printf("exit: waited=%d status=%d global=%d\n", waited, status, global);

	if (fcntl(pipe_fds[0], F_SETFL, 0) < 0) {
		perror("fcntl");
		return 1;
	}
	fflush(stdout);
	pid = clone(exec_reader, top, CLONE_VFORK | SIGCHLD, NULL);
	int released = pid > 0 && write(pipe_fds[1], "y", 1) == 1;
	status = reap(pid);
	printf("exec: released=%d status=%d\n", released, status);
	return 0;
}
