// clone3 in the asyncify build configuration: a struct clone_args of any size from the first
// version on, with zeros past what the kernel knows (else E2BIG); the flags clone3_args_valid
// rejects; and a fork, which returns the child's pid in the parent after CLONE_PARENT_SETTID's
// write and 0 in the child, whose exit status and exit signal the parent then sees.
// CLONE_CLEAR_SIGHAND gives the child default actions for caught signals and keeps ignored ones.
//#BuildEnv: WASIXCC_WASM_EXCEPTIONS=no
//#ExpectedStdout: sizes ok
//#ExpectedStdout: invalid ok
//#ExpectedStdout: fork: ptid-ok=1 status=5 sigchld=1
//#ExpectedStdout: clear-sighand: status=0
#define _GNU_SOURCE
#include <errno.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef SYS_clone3
#define SYS_clone3 435
#endif
#ifndef CLONE_CLEAR_SIGHAND
#define CLONE_CLEAR_SIGHAND 0x100000000ULL
#endif

/// `struct clone_args`, CLONE_ARGS_SIZE_VER2.
struct args {
	uint64_t flags, pidfd, child_tid, parent_tid, exit_signal, stack, stack_size, tls;
	uint64_t set_tid, set_tid_size, cgroup;
};

static int fails;
#define FAILS(call, e) do { errno = 0; long r_ = (long)(call); if (r_ != -1 || errno != (e)) { fprintf(stderr, "line %d: %s = %ld (%s), want -1 %s\n", __LINE__, #call, r_, strerror(errno), #e); fails++; } } while (0)

static long clone3(void *args, size_t size)
{
	return syscall(SYS_clone3, args, size);
}

static volatile sig_atomic_t sigchld;

static void on_sigchld(int sig)
{
	(void)sig;
	sigchld = 1;
}

static void on_usr1(int sig)
{
	(void)sig;
}

/// Reaps `pid`, which must have exited; its status, or -1.
static int reap(long pid)
{
	int status;
	if (pid <= 0 || waitpid(pid, &status, 0) != pid || !WIFEXITED(status))
		return -1;
	return WEXITSTATUS(status);
}

int main(void)
{
	// Room for a larger struct, zeroed past the known version.
	static union {
		struct args args;
		unsigned char bytes[128];
	} big;
	struct args *args = &big.args;

	FAILS(clone3(args, 63), EINVAL);
	FAILS(clone3(args, 70000), E2BIG);
	big.bytes[sizeof *args + 3] = 1;
	FAILS(clone3(args, sizeof big.bytes), E2BIG);
	big.bytes[sizeof *args + 3] = 0;
	puts(fails ? "sizes FAILED" : "sizes ok");

	int before = fails;
	args->exit_signal = 0x100;
	FAILS(clone3(args, sizeof *args), EINVAL);
	args->exit_signal = SIGCHLD;
	args->flags = SIGCHLD;
	FAILS(clone3(args, sizeof *args), EINVAL);
	args->flags = CLONE_PARENT;
	FAILS(clone3(args, sizeof *args), EINVAL);
	args->flags = CLONE_NEWNS | CLONE_FS;
	FAILS(clone3(args, sizeof *args), EINVAL);
	args->flags = 0;
	args->stack = 4096;
	FAILS(clone3(args, sizeof *args), EINVAL);
	args->stack = 0;
	args->stack_size = 4096;
	FAILS(clone3(args, sizeof *args), EINVAL);
	args->stack_size = 0;
	puts(fails == before ? "invalid ok" : "invalid FAILED");

	signal(SIGCHLD, on_sigchld);
	pid_t parent = getpid();
	int ptid = 0;
	args->flags = CLONE_PARENT_SETTID;
	args->parent_tid = (uintptr_t)&ptid;
	fflush(stdout);
	long pid = clone3(big.bytes, sizeof big.bytes);
	if (pid == 0)
		_exit(getppid() == parent ? 5 : 1);
	int status = reap(pid);
	printf("fork: ptid-ok=%d status=%d sigchld=%d\n", pid > 0 && ptid == pid, status, sigchld);

	signal(SIGUSR1, on_usr1);
	signal(SIGUSR2, SIG_IGN);
	args->flags = CLONE_CLEAR_SIGHAND;
	args->parent_tid = 0;
	fflush(stdout);
	pid = clone3(args, sizeof *args);
	if (pid == 0) {
		struct sigaction usr1, usr2;
		sigaction(SIGUSR1, NULL, &usr1);
		sigaction(SIGUSR2, NULL, &usr2);
		_exit(usr1.sa_handler == SIG_DFL && usr2.sa_handler == SIG_IGN ? 0 : 1);
	}
	printf("clear-sighand: status=%d\n", reap(pid));
	return fails;
}
