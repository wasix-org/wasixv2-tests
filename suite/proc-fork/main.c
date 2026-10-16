// fork in the asyncify build configuration: it returns the child's pid in the parent and 0 in the
// child, which has its own pid, the parent as its parent, a copy of the parent's memory (stack,
// globals and heap) that the two then change apart, and the parent's descriptors, sharing their
// open files. The parent reaps it with its status, and a SIGCHLD tells it the child ended.
//#BuildEnv: WASIXCC_WASM_EXCEPTIONS=no
//#ExpectedStdout: child: pid-ok=1 ppid-ok=1 stack=7 global=100 heap=hello
//#ExpectedStdout: parent: pid-ok=1 stack=7 global=100 heap=hello
//#ExpectedStdout: pipe: from the child
//#ExpectedStdout: offset shared=1
//#ExpectedStdout: status=3 sigchld=1
//#ExpectedStdout: brk-ok=1
//#ExpectedStdout: forks=8 ok
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

static int global = 100;
static volatile sig_atomic_t sigchld;

static void on_chld(int sig)
{
	(void)sig;
	sigchld = 1;
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
	signal(SIGCHLD, on_chld);
	pid_t me = getpid();
	int stack = 7;
	char *heap = strdup("hello");
	int fds[2];
	check(pipe(fds) == 0, "pipe");
	int file = open("/tmp/fork-offset", O_RDWR | O_CREAT | O_TRUNC, 0600);
	check(file >= 0, "open");

	// The child starts at the parent's break, and moves only its own.
	long base = syscall(SYS_brk, 0);
	long grown = syscall(SYS_brk, base + (1 << 16));
	check(grown == base + (1 << 16), "brk");
	pid_t pid = fork();
	check(pid >= 0, "fork");
	if (pid == 0) _exit(syscall(SYS_brk, 0) == grown && syscall(SYS_brk, base) == base ? 0 : 1);
	int status;
	check(waitpid(pid, &status, 0) == pid, "waitpid");
	int brk_ok = WIFEXITED(status) && WEXITSTATUS(status) == 0 && syscall(SYS_brk, 0) == grown;
	sigchld = 0;

	fflush(stdout);
	pid = fork();
	check(pid >= 0, "fork");
	if (pid == 0) {
		printf("child: pid-ok=%d ppid-ok=%d stack=%d global=%d heap=%s\n", getpid() != me,
		       getppid() == me, stack, global, heap);
		stack = 1;
		global = 1;
		strcpy(heap, "child");
		close(fds[0]);
		const char msg[] = "from the child";
		check(write(fds[1], msg, sizeof msg) == sizeof msg, "write");
		check(write(file, "abcd", 4) == 4, "write file");
		fflush(stdout);
		_exit(3);
	}
	close(fds[1]);
	check(waitpid(pid, &status, 0) == pid, "waitpid");
	printf("parent: pid-ok=%d stack=%d global=%d heap=%s\n", pid != me, stack, global, heap);
	char buf[32] = {0};
	check(read(fds[0], buf, sizeof buf) > 0, "read");
	printf("pipe: %s\n", buf);
	printf("offset shared=%d\n", lseek(file, 0, SEEK_CUR) == 4);
	printf("status=%d sigchld=%d\n", WIFEXITED(status) ? WEXITSTATUS(status) : -1, sigchld);
	printf("brk-ok=%d\n", brk_ok);

	// Children of children, and many in a row.
	int forks = 0;
	for (int i = 0; i < 4; i++) {
		pid = fork();
		check(pid >= 0, "fork");
		if (pid == 0) {
			pid_t grandchild = fork();
			if (grandchild == 0) _exit(2);
			int st;
			if (waitpid(grandchild, &st, 0) != grandchild) _exit(1);
			_exit(WEXITSTATUS(st));
		}
		check(waitpid(pid, &status, 0) == pid, "waitpid");
		forks += 2 * (WIFEXITED(status) && WEXITSTATUS(status) == 2);
	}
	printf("forks=%d ok\n", forks);
	return 0;
}
