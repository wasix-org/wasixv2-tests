// setjmp and longjmp in the asyncify build configuration, where they are the host's stack-save
// and stack-restore: longjmp returns from setjmp again with its value (1 for 0), from deep in
// the call stack, keeping memory as it is; siglongjmp leaves a signal handler and restores the
// signal mask sigsetjmp saved; a handler can use setjmp and longjmp inside itself; and a forked
// child can longjmp to a point saved before the fork.
//#BuildEnv: WASIXCC_WASM_EXCEPTIONS=no
//#ExpectedStdout: deep: val=42 memory=5
//#ExpectedStdout: zero: val=1
//#ExpectedStdout: again: rounds=3
//#ExpectedStdout: handler: jumped=1 mask-restored=1
//#ExpectedStdout: inside handler: val=7 returned=1
//#ExpectedStdout: child: val=11
//#ExpectedStdout: parent: val=0 status=0
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

static jmp_buf jb;
static sigjmp_buf sjb;
static volatile int memory;

static void deep(int n, int val)
{
	memory = n;
	if (n == 5) longjmp(jb, val);
	deep(n + 1, val);
}

static void on_usr1(int sig)
{
	(void)sig;
	siglongjmp(sjb, 1);
}

static volatile int inner_returned;

static void on_usr2(int sig)
{
	(void)sig;
	jmp_buf local;
	int val = setjmp(local);
	if (val == 0) longjmp(local, 7);
	printf("inside handler: val=%d", val);
	inner_returned = 1;
}

int main(void)
{
	int val = setjmp(jb);
	if (val == 0) deep(0, 42);
	printf("deep: val=%d memory=%d\n", val, memory);

	val = setjmp(jb);
	if (val == 0) longjmp(jb, 0);
	printf("zero: val=%d\n", val);

	volatile int rounds = 0;
	val = setjmp(jb);
	if (rounds < 3) {
		rounds++;
		longjmp(jb, rounds);
	}
	printf("again: rounds=%d\n", rounds);

	signal(SIGUSR1, on_usr1);
	sigset_t none, now;
	sigemptyset(&none);
	sigprocmask(SIG_SETMASK, &none, NULL);
	volatile int jumped = 0;
	if (sigsetjmp(sjb, 1) == 0) {
		raise(SIGUSR1);
	} else {
		jumped = 1;
	}
	sigprocmask(SIG_SETMASK, NULL, &now);
	printf("handler: jumped=%d mask-restored=%d\n", jumped, !sigismember(&now, SIGUSR1));

	signal(SIGUSR2, on_usr2);
	raise(SIGUSR2);
	printf(" returned=%d\n", inner_returned);

	val = setjmp(jb);
	if (val != 0) {
		printf("child: val=%d\n", val);
		fflush(stdout);
		_exit(0);
	}
	fflush(stdout);
	pid_t pid = fork();
	if (pid < 0) {
		perror("fork");
		return 1;
	}
	if (pid == 0) longjmp(jb, 11);
	int status;
	if (waitpid(pid, &status, 0) != pid) {
		perror("waitpid");
		return 1;
	}
	printf("parent: val=%d status=%d\n", val, WEXITSTATUS(status));
	return 0;
}
