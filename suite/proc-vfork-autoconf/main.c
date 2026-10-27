// What autoconf's AC_FUNC_FORK needs: vfork is a function libc links, so its link test finds
// it; a call that bypasses the vfork macro reaches it and forks. A program whose config.h
// defines vfork itself (`#define vfork fork`, written when the test fails) keeps its own
// definition, and <unistd.h> leaves fork alone.
//#BuildEnv: WASIXCC_WASM_EXCEPTIONS=no
//#ExpectedStdout: fork: status=2
//#ExpectedStdout: defined: status=3
//#ExpectedStdout: function: status=4
#define vfork fork
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

static int status_of(pid_t pid, int code)
{
	if (pid < 0) {
		perror("spawn");
		return -1;
	}
	if (pid == 0)
		_exit(code);
	int status;
	if (waitpid(pid, &status, 0) != pid) {
		perror("waitpid");
		return -1;
	}
	return WEXITSTATUS(status);
}

static int plain(void)
{
	fflush(stdout);
	return status_of(fork(), 2);
}

// config.h's vfork, which is fork.
static int defined(void)
{
	fflush(stdout);
	return status_of(vfork(), 3);
}

#undef vfork

// As autoconf's AC_CHECK_FUNCS declares it, bypassing the macro.
pid_t vfork(void);

static int function(void)
{
	pid_t (*spawn)(void) = vfork;
	fflush(stdout);
	return status_of(spawn(), 4);
}

int main(void)
{
	printf("fork: status=%d\n", plain());
	printf("defined: status=%d\n", defined());
	printf("function: status=%d\n", function());
	return 0;
}
