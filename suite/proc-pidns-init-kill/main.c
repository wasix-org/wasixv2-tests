// Killing a pid namespace's init kills its namespace: the init (pid 1 there) forks two
// children, one of them the init of a nested namespace, which forks one more; SIGKILL from
// inside does nothing to the init, but from the caller, outside, it kills it, and once the
// caller reaps it none of the four is left (zap_pid_ns_processes). Without CAP_SYS_ADMIN
// (a uid other than 0), clone with CLONE_NEWPID fails with EPERM.
//#BuildEnv: WASIXCC_WASM_EXCEPTIONS=no
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: CHECK(%s) failed (errno %s)\n", __LINE__, #c, strerror(errno)); fails++; } } while (0)

/// fork with `flags` (`clone(flags | SIGCHLD)`).
static pid_t fork_with(unsigned long flags)
{
	return (pid_t)syscall(SYS_clone, flags | SIGCHLD, 0, NULL, NULL, 0);
}

/// Runs until killed, after telling `ready` it runs.
static void sleeper(int ready)
{
	char c = 1;
	if (write(ready, &c, 1) != 1)
		_exit(2);
	for (;;)
		pause();
}

/// The pids of the caller's processes whose parent is `parent`, as /proc lists them.
static int children_of(pid_t parent, pid_t *out, int max)
{
	DIR *dir = opendir("/proc");
	if (!dir)
		return 0;
	int count = 0;
	struct dirent *entry;
	while ((entry = readdir(dir)) && count < max) {
		char *end;
		long pid = strtol(entry->d_name, &end, 10);
		if (*end || pid <= 0)
			continue;
		char path[64], buf[512];
		snprintf(path, sizeof path, "/proc/%ld/stat", pid);
		int fd = open(path, O_RDONLY);
		if (fd < 0)
			continue;
		ssize_t n = read(fd, buf, sizeof buf - 1);
		close(fd);
		buf[n > 0 ? n : 0] = 0;
		char *paren = strrchr(buf, ')');
		int ppid;
		char state;
		if (paren && sscanf(paren + 2, "%c %d", &state, &ppid) == 2 && ppid == parent)
			out[count++] = (pid_t)pid;
	}
	closedir(dir);
	return count;
}

int main(void)
{
	if (geteuid() != 0) {
		errno = 0;
		pid_t pid = fork_with(CLONE_NEWPID);
		if (pid == 0)
			_exit(0);
		CHECK(pid == -1 && errno == EPERM);
	} else {
		int ready[2];
		CHECK(pipe(ready) == 0);
		pid_t init = fork_with(CLONE_NEWPID);
		if (init == 0) {
			close(ready[0]);
			// SIGKILL from inside, even its own, does not reach it.
			kill(1, SIGKILL);
			if (fork() == 0)
				sleeper(ready[1]);
			if (fork_with(CLONE_NEWPID) == 0) {
				if (fork() == 0)
					sleeper(ready[1]);
				sleeper(ready[1]);
			}
			sleeper(ready[1]);
		}
		close(ready[1]);
		char c;
		for (int i = 0; i < 4; i++)
			CHECK(read(ready[0], &c, 1) == 1);
		pid_t inside[8], below[8];
		int n = children_of(init, inside, 8);
		CHECK(n == 2);
		int deeper = 0;
		for (int i = 0; i < n; i++)
			deeper += children_of(inside[i], below + deeper, 8 - deeper);
		CHECK(deeper == 1);
		CHECK(kill(init, SIGKILL) == 0);
		int status;
		CHECK(waitpid(init, &status, 0) == init);
		CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
		for (int i = 0; i < n; i++) {
			errno = 0;
			CHECK(kill(inside[i], 0) == -1 && errno == ESRCH);
		}
		errno = 0;
		CHECK(kill(below[0], 0) == -1 && errno == ESRCH);
	}
	if (fails)
		return 1;
	puts("PASS");
	return 0;
}
