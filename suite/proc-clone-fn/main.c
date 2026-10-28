// clone(fn, stack, flags, arg) without CLONE_VM in the asyncify build configuration: a fork with
// the given flags, whose child runs fn(arg) and exits with its result (the low byte, as exit's);
// the parent gets the child's pid. The child has a copy of the parent's memory; CLONE_PARENT_SETTID
// writes the pid in the parent's, CLONE_CHILD_SETTID in the child's; CLONE_FILES shares the
// descriptor table and CLONE_FS the working directory. musl's wrapper refuses a null stack and
// CLONE_THREAD with EINVAL, and CLONE_VM | CLONE_VFORK runs fn as a vfork child.
//#BuildEnv: WASIXCC_WASM_EXCEPTIONS=no
//#ExpectedStdout: fork: pid-ok=1 status=7 sigchld=1 global=100
//#ExpectedStdout: parent-settid: ok=1 status=0
//#ExpectedStdout: child-settid: status=0
//#ExpectedStdout: clone-files: status=0 closed=1
//#ExpectedStdout: clone-fs: status=0 moved=1
//#ExpectedStdout: invalid ok
//#ExpectedStdout: vfork: status=4
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define STACK_SIZE 65536

static char stack[STACK_SIZE] __attribute__((aligned(16)));
static void *const top = stack + STACK_SIZE;

static int global = 100;
static pid_t parent;
static volatile sig_atomic_t sigchld;

static int fails;
#define FAILS(call, e) do { errno = 0; long r_ = (long)(call); if (r_ != -1 || errno != (e)) { fprintf(stderr, "line %d: %s = %ld (%s), want -1 %s\n", __LINE__, #call, r_, strerror(errno), #e); fails++; } } while (0)

static void on_sigchld(int sig)
{
	(void)sig;
	sigchld = 1;
}

/// Reaps `pid`, which must have exited; its status, or -1.
static int reap(pid_t pid)
{
	int status;
	if (pid <= 0 || waitpid(pid, &status, 0) != pid || !WIFEXITED(status))
		return -1;
	return WEXITSTATUS(status);
}

static int fork_child(void *arg)
{
	global = 200;
	if (getppid() != parent || getpid() == parent || strcmp(arg, "arg") != 0)
		return 1;
	return 256 + 7;
}

static int nothing(void *arg)
{
	(void)arg;
	return 0;
}

static int check_ctid(void *arg)
{
	return *(pid_t *)arg == getpid() ? 0 : 1;
}

static int close_fd(void *arg)
{
	return close(*(int *)arg);
}

static int to_root(void *arg)
{
	(void)arg;
	return chdir("/");
}

static int vfork_child(void *arg)
{
	(void)arg;
	_exit(4);
}

int main(void)
{
	parent = getpid();
	signal(SIGCHLD, on_sigchld);
	fflush(stdout);
	pid_t pid = clone(fork_child, top, SIGCHLD, "arg");
	int status = reap(pid);
	printf("fork: pid-ok=%d status=%d sigchld=%d global=%d\n", pid > 0, status, sigchld, global);

	pid_t ptid = 0;
	fflush(stdout);
	pid = clone(nothing, top, CLONE_PARENT_SETTID | SIGCHLD, NULL, &ptid);
	status = reap(pid);
	printf("parent-settid: ok=%d status=%d\n", pid > 0 && ptid == pid, status);

	pid_t ctid = 0;
	fflush(stdout);
	pid = clone(check_ctid, top, CLONE_CHILD_SETTID | SIGCHLD, &ctid, NULL, NULL, &ctid);
	status = reap(pid);
	printf("child-settid: status=%d\n", ctid == 0 ? status : -1);

	int fd = open("/", O_RDONLY | O_DIRECTORY);
	fflush(stdout);
	pid = clone(close_fd, top, CLONE_FILES | SIGCHLD, &fd);
	status = reap(pid);
	int closed = fcntl(fd, F_GETFD) < 0 && errno == EBADF;
	printf("clone-files: status=%d closed=%d\n", status, closed);

	char dir[] = "/tmp/proc-clone-fn.XXXXXX";
	if (!mkdtemp(dir) || chdir(dir) < 0) {
		perror(dir);
		return 1;
	}
	fflush(stdout);
	pid = clone(to_root, top, CLONE_FS | SIGCHLD, NULL);
	status = reap(pid);
	struct stat here, root;
	int moved = stat(".", &here) == 0 && stat("/", &root) == 0 && here.st_dev == root.st_dev
		&& here.st_ino == root.st_ino;
	printf("clone-fs: status=%d moved=%d\n", status, moved);
	rmdir(dir);

	FAILS(clone(nothing, NULL, SIGCHLD, NULL), EINVAL);
	FAILS(clone(nothing, top, CLONE_THREAD | CLONE_SIGHAND | CLONE_VM, NULL), EINVAL);
	puts(fails ? "invalid FAILED" : "invalid ok");

	fflush(stdout);
	pid = clone(vfork_child, top, CLONE_VM | CLONE_VFORK | SIGCHLD, NULL);
	printf("vfork: status=%d\n", reap(pid));
	return fails;
}
