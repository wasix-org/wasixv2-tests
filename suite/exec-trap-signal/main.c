// A trap is a hardware fault: the process dies of its signal with the default action, whether
// the main thread, another thread or a signal handler traps, so waitpid sees WIFSIGNALED with
// SIGSEGV for a bad access, SIGILL for a trap instruction and SIGFPE for a division by zero.
//#MappedDirectory: .:/t
//#ExpectedStdout: segv: signaled=1 sig=11
//#ExpectedStdout: ill: signaled=1 sig=4
//#ExpectedStdout: fpe: signaled=1 sig=8
//#ExpectedStdout: thread: signaled=1 sig=11
//#ExpectedStdout: handler: signaled=1 sig=11
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

static volatile int zero;

/* Past the end of any memory a wasm32 module can have below 4 GiB. */
static void bad_access(void)
{
	*(volatile int *)0xfffffff0u = 1;
}

static void *trap_in_thread(void *arg)
{
	bad_access();
	return arg;
}

static void trap_in_handler(int sig)
{
	(void)sig;
	bad_access();
}

static int child(const char *mode)
{
	if (strcmp(mode, "segv") == 0)
		bad_access();
	if (strcmp(mode, "ill") == 0)
		__builtin_trap();
	if (strcmp(mode, "fpe") == 0)
		return 1 / zero;
	if (strcmp(mode, "thread") == 0) {
		pthread_t t;
		pthread_create(&t, 0, trap_in_thread, 0);
		pthread_join(t, 0);
	}
	if (strcmp(mode, "handler") == 0) {
		signal(SIGUSR1, trap_in_handler);
		raise(SIGUSR1);
	}
	printf("%s: survived\n", mode);
	return 0;
}

static void run(char *mode)
{
	char *args[] = {"/t/main", mode, NULL};
	pid_t pid;
	int status;
	if (posix_spawn(&pid, "/t/main", NULL, NULL, args, environ) != 0 ||
	    waitpid(pid, &status, 0) != pid) {
		perror(mode);
		exit(1);
	}
	printf("%s: signaled=%d sig=%d\n", mode, WIFSIGNALED(status), WTERMSIG(status));
	fflush(stdout);
}

int main(int argc, char **argv)
{
	if (argc > 1)
		return child(argv[1]);
	run("segv");
	run("ill");
	run("fpe");
	run("thread");
	run("handler");
	return 0;
}
