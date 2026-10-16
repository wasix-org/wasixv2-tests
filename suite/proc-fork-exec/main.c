// A forked child that calls execve keeps its pid and its parent, and loses the descriptors marked
// close-on-exec; the parent waits for the new image's status, then forks and execs again, with
// a shell-like pipeline: two children connected by a pipe.
//#BuildEnv: WASIXCC_WASM_EXCEPTIONS=no
//#MappedDirectory: .:/t
//#ExpectedStdout: exec'd: same-pid=1 ppid-ok=1 kept=1 cloexec-closed=1
//#ExpectedStdout: status=9
//#ExpectedStdout: pipeline: through the pipe
//#ExpectedStdout: statuses=0,0
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static void check(int ok, const char *what)
{
	if (!ok) {
		perror(what);
		exit(1);
	}
}

static int run_child(int argc, char **argv)
{
	if (strcmp(argv[1], "exec") == 0) {
		int kept = fcntl(atoi(argv[4]), F_GETFD) >= 0;
		int closed = fcntl(atoi(argv[5]), F_GETFD) < 0 && errno == EBADF;
		printf("exec'd: same-pid=%d ppid-ok=%d kept=%d cloexec-closed=%d\n",
		       getpid() == atoi(argv[2]), getppid() == atoi(argv[3]), kept, closed);
		return 9;
	}
	if (strcmp(argv[1], "write") == 0) {
		fputs("through the pipe\n", stdout);
		return 0;
	}
	if (strcmp(argv[1], "read") == 0) {
		char line[64];
		if (!fgets(line, sizeof line, stdin)) return 1;
		printf("pipeline: %s", line);
		return 0;
	}
	(void)argc;
	return 2;
}

int main(int argc, char **argv)
{
	if (argc > 1) return run_child(argc, argv);
	int keep = dup(1);
	int drop = fcntl(1, F_DUPFD_CLOEXEC, 10);
	check(keep >= 0 && drop >= 0, "dup");
	char ppid[16];
	snprintf(ppid, sizeof ppid, "%d", getpid());

	fflush(stdout);
	pid_t pid = fork();
	check(pid >= 0, "fork");
	if (pid == 0) {
		char self[16], k[16], d[16];
		snprintf(self, sizeof self, "%d", getpid());
		snprintf(k, sizeof k, "%d", keep);
		snprintf(d, sizeof d, "%d", drop);
		char *args[] = {"/t/main", "exec", self, ppid, k, d, NULL};
		execv("/t/main", args);
		_exit(127);
	}
	int status;
	check(waitpid(pid, &status, 0) == pid, "waitpid");
	printf("status=%d\n", WIFEXITED(status) ? WEXITSTATUS(status) : -1);

	int fds[2];
	check(pipe(fds) == 0, "pipe");
	fflush(stdout);
	pid_t writer = fork();
	check(writer >= 0, "fork");
	if (writer == 0) {
		dup2(fds[1], 1);
		close(fds[0]);
		close(fds[1]);
		execl("/t/main", "/t/main", "write", (char *)NULL);
		_exit(127);
	}
	pid_t reader = fork();
	check(reader >= 0, "fork");
	if (reader == 0) {
		dup2(fds[0], 0);
		close(fds[0]);
		close(fds[1]);
		execl("/t/main", "/t/main", "read", (char *)NULL);
		_exit(127);
	}
	close(fds[0]);
	close(fds[1]);
	int ws, rs;
	check(waitpid(writer, &ws, 0) == writer, "waitpid");
	check(waitpid(reader, &rs, 0) == reader, "waitpid");
	printf("statuses=%d,%d\n", WEXITSTATUS(ws), WEXITSTATUS(rs));
	return 0;
}
