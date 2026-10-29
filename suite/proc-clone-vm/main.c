// clone(fn, stack, CLONE_VM | ..., arg) without CLONE_THREAD and CLONE_VFORK: a process of its
// own (its pid, its parent the caller, reaped with its exit signal) that shares the caller's
// memory, so the caller sees what it wrote; CLONE_PARENT_SETTID writes its pid there.
// CLONE_SIGHAND shares the signal handlers too, so a handler the child installs runs in the
// parent. The child can execve, which starts the new image on memory of its own, and fork
// (the asyncify build configuration), whose child gets a copy of the shared memory.
//#BuildEnv: WASIXCC_WASM_EXCEPTIONS=no
//#MappedDirectory: .:/t
//#ExpectedStdout: vm: status=3 shared=200 pid-ok=1 ptid-ok=1
//#ExpectedStdout: sighand: status=0 handled=1
//#ExpectedStdout: exec child: ok
//#ExpectedStdout: exec: status=9 shared=200
//#ExpectedStdout: fork: status=14 shared=300
#define _GNU_SOURCE
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

static volatile int shared = 100;
static pid_t parent;
static pid_t child_pid;
static volatile sig_atomic_t handled;

/// Reaps `pid`; its exit status, or -1.
static int reap(pid_t pid)
{
	int status;
	if (pid <= 0 || waitpid(pid, &status, __WALL) != pid || !WIFEXITED(status))
		return -1;
	return WEXITSTATUS(status);
}

static int write_shared(void *arg)
{
	(void)arg;
	shared = 200;
	child_pid = getpid();
	return getppid() == parent && getpid() != parent ? 3 : 1;
}

static void on_usr1(int sig)
{
	(void)sig;
	handled = 1;
}

static int install_handler(void *arg)
{
	(void)arg;
	struct sigaction sa = {.sa_handler = on_usr1};
	return sigaction(SIGUSR1, &sa, NULL) == 0 ? 0 : 1;
}

static int exec_child(void *arg)
{
	(void)arg;
	char *args[] = {SELF, "exec-child", NULL};
	execv(SELF, args);
	return 127;
}

static int fork_grandchild(void *arg)
{
	(void)arg;
	pid_t pid = fork();
	if (pid == 0) {
		// The grandchild's memory is a copy: the change stays its own.
		shared = 999;
		_exit(4);
	}
	int status = reap(pid);
	shared = 300;
	return status < 0 ? 1 : status + 10;
}

int main(int argc, char **argv)
{
	if (argc > 1 && strcmp(argv[1], "exec-child") == 0) {
		puts("exec child: ok");
		return 9;
	}
	parent = getpid();

	pid_t ptid = 0;
	fflush(stdout);
	pid_t pid = clone(write_shared, top, CLONE_VM | CLONE_PARENT_SETTID | SIGCHLD, NULL, &ptid);
	int status = reap(pid);
	printf("vm: status=%d shared=%d pid-ok=%d ptid-ok=%d\n", status, shared,
	       pid > 0 && child_pid == pid, ptid == pid);

	fflush(stdout);
	pid = clone(install_handler, top, CLONE_VM | CLONE_SIGHAND | SIGCHLD, NULL);
	status = reap(pid);
	raise(SIGUSR1);
	printf("sighand: status=%d handled=%d\n", status, handled);

	fflush(stdout);
	pid = clone(exec_child, top, CLONE_VM | SIGCHLD, NULL);
	status = reap(pid);
	printf("exec: status=%d shared=%d\n", status, shared);

	fflush(stdout);
	pid = clone(fork_grandchild, top, CLONE_VM | SIGCHLD, NULL);
	status = reap(pid);
	printf("fork: status=%d shared=%d\n", status, shared);
	return 0;
}
