// The first 64 KiB of a program's memory are a guard, as below Linux's mmap_min_addr: a store
// through a null pointer, or to any address below 65536, kills the process with SIGSEGV instead
// of overwriting whatever lives at address 0. The page right above the guard, the bottom of the
// main stack, is ordinary memory.
//
// Loads there read zeros for now, unlike Linux: clang's wasm32 backend passes a null va_list
// buffer to variadic calls without variadic arguments, which musl reads anyway (fcntl(fd,
// F_GETFD), posix_spawn's clone). The follow-up in wasmer's lib/wasixv2/DESIGN.md, "Guard page
// at 0", makes them fault too; the read cases below then expect SIGSEGV.
//#MappedDirectory: .:/t
//#ExpectedStdout: write-null: signaled=1 sig=11
//#ExpectedStdout: write-4096: signaled=1 sig=11
//#ExpectedStdout: write-last: signaled=1 sig=11
//#ExpectedStdout: read-null: exited=1 status=0
//#ExpectedStdout: read-last: exited=1 status=0
//#ExpectedStdout: thread: signaled=1 sig=11
//#ExpectedStdout: above: exited=1 status=0
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

extern char **environ;

static volatile int *ptr(uintptr_t addr)
{
	/* Through a volatile so the compiler cannot see a null dereference and drop it. */
	static volatile uintptr_t a;
	a = addr;
	return (volatile int *)a;
}

static void *write_null(void *arg)
{
	*ptr(0) = 1;
	return arg;
}

static int child(const char *mode)
{
	if (strcmp(mode, "write-null") == 0)
		write_null(0);
	if (strcmp(mode, "write-4096") == 0)
		*ptr(4096) = 1;
	if (strcmp(mode, "write-last") == 0)
		*ptr(65536 - sizeof(int)) = 1;
	/* Zeros, as nothing can be written there. */
	if (strcmp(mode, "read-null") == 0)
		return *ptr(0) != 0;
	if (strcmp(mode, "read-last") == 0)
		return *ptr(65536 - sizeof(int)) != 0;
	if (strcmp(mode, "thread") == 0) {
		pthread_t t;
		pthread_create(&t, 0, write_null, 0);
		pthread_join(t, 0);
	}
	if (strcmp(mode, "above") == 0) {
		/* Unused this far from the top of an 8 MiB stack. */
		*ptr(65536) = 7;
		return *ptr(65536) == 7 ? 0 : 1;
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
	if (WIFSIGNALED(status))
		printf("%s: signaled=1 sig=%d\n", mode, WTERMSIG(status));
	else
		printf("%s: exited=%d status=%d\n", mode, WIFEXITED(status), WEXITSTATUS(status));
	fflush(stdout);
}

int main(int argc, char **argv)
{
	if (argc > 1)
		return child(argv[1]);
	run("write-null");
	run("write-4096");
	run("write-last");
	run("read-null");
	run("read-last");
	run("thread");
	run("above");
	return 0;
}
