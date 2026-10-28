// A minimal container: unshare(CLONE_NEWPID | CLONE_NEWNS), then fork the namespace's init,
// which makes its mounts private, mounts a /proc of its own (`mount -t proc`) and lists it as
// `ps` would: only the namespace's processes, by their pids there, the init as pid 1 with
// parent 0. The caller keeps its own pid, and its /proc still shows its own view, where the
// init has another pid. Without CAP_SYS_ADMIN (a uid other than 0), unshare fails with EPERM.
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
#include <sys/mount.h>
#include <sys/wait.h>
#include <unistd.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: CHECK(%s) failed (errno %s)\n", __LINE__, #c, strerror(errno)); fails++; } } while (0)
#define FAILS(call, e) do { errno = 0; long r_ = (long)(call); if (r_ != -1 || errno != (e)) { fprintf(stderr, "line %d: %s = %ld (%s), want -1 %s\n", __LINE__, #call, r_, strerror(errno), #e); fails++; } } while (0)

/// The parent pid and name `/proc/<pid>/stat` shows, as `ps` reads them.
static int stat_of(const char *pid, int *ppid, char *comm, size_t size)
{
	char path[64], buf[512];
	snprintf(path, sizeof path, "/proc/%s/stat", pid);
	int fd = open(path, O_RDONLY);
	if (fd < 0)
		return -1;
	ssize_t n = read(fd, buf, sizeof buf - 1);
	close(fd);
	if (n <= 0)
		return -1;
	buf[n] = 0;
	char *open_paren = strchr(buf, '('), *close_paren = strrchr(buf, ')');
	if (!open_paren || !close_paren)
		return -1;
	snprintf(comm, size, "%.*s", (int)(close_paren - open_paren - 1), open_paren + 1);
	char state;
	return sscanf(close_paren + 2, "%c %d", &state, ppid) == 2 ? 0 : -1;
}

/// `ps -e -o pid=,ppid=` of /proc: "pid:ppid " for each process, lowest pid first.
static void ps(char *out, size_t size)
{
	out[0] = 0;
	DIR *dir = opendir("/proc");
	if (!dir)
		return;
	int pids[64], count = 0;
	struct dirent *entry;
	while ((entry = readdir(dir)) && count < 64) {
		char *end;
		long pid = strtol(entry->d_name, &end, 10);
		if (*end == 0 && pid > 0)
			pids[count++] = (int)pid;
	}
	closedir(dir);
	for (int i = 0; i < count; i++)
		for (int j = i + 1; j < count; j++)
			if (pids[j] < pids[i]) {
				int t = pids[i];
				pids[i] = pids[j];
				pids[j] = t;
			}
	for (int i = 0; i < count; i++) {
		char name[16], comm[64];
		int ppid = -1;
		snprintf(name, sizeof name, "%d", pids[i]);
		if (stat_of(name, &ppid, comm, sizeof comm) == 0)
			snprintf(out + strlen(out), size - strlen(out), "%d:%d ", pids[i], ppid);
	}
}

/// Blocks until the other end of `fd` writes or closes.
static void wait_on(int fd)
{
	char c;
	while (read(fd, &c, 1) < 0 && errno == EINTR) {
	}
}

/// The namespace's init: pid 1, with a /proc of its own that lists it, a child and a
/// grandchild by their pids here.
static int init(int report)
{
	CHECK(getpid() == 1);
	CHECK(getppid() == 0);
	CHECK(mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) == 0);
	CHECK(mount("proc", "/proc", "proc", 0, NULL) == 0);
	int sync[2];
	CHECK(pipe(sync) == 0);
	pid_t child = fork();
	if (child == 0) {
		close(sync[1]);
		pid_t grandchild = fork();
		if (grandchild == 0) {
			wait_on(sync[0]);
			_exit(0);
		}
		wait_on(sync[0]);
		waitpid(grandchild, NULL, 0);
		_exit(0);
	}
	CHECK(child == 2);
	close(sync[0]);
	// The grandchild is there once /proc lists it.
	char listing[512];
	for (int i = 0; i < 1000; i++) {
		ps(listing, sizeof listing);
		if (strstr(listing, "3:2 "))
			break;
		usleep(1000);
	}
	if (strcmp(listing, "1:0 2:1 3:2 ") != 0) {
		fprintf(stderr, "ps in the namespace: %s\n", listing);
		fails++;
	}
	char self[32] = "";
	ssize_t n = readlink("/proc/self", self, sizeof self - 1);
	self[n > 0 ? n : 0] = 0;
	CHECK(strcmp(self, "1") == 0);
	close(sync[1]);
	int status;
	CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status));
	CHECK(write(report, &fails, sizeof fails) == sizeof fails);
	return fails;
}

int main(void)
{
	pid_t me = getpid();
	if (geteuid() != 0) {
		FAILS(unshare(CLONE_NEWPID | CLONE_NEWNS), EPERM);
	} else {
		int report[2];
		CHECK(pipe(report) == 0);
		CHECK(unshare(CLONE_NEWPID | CLONE_NEWNS) == 0);
		CHECK(getpid() == me);
		pid_t pid = fork();
		if (pid == 0) {
			close(report[0]);
			_exit(init(report[1]) ? 1 : 0);
		}
		close(report[1]);
		CHECK(pid > 1 && pid != me);
		// The caller's /proc is its own: there the init has the pid fork returned.
		char path[64];
		snprintf(path, sizeof path, "/proc/%d", pid);
		CHECK(access(path, F_OK) == 0);
		int status, inner = -1;
		CHECK(waitpid(pid, &status, 0) == pid);
		CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
		CHECK(read(report[0], &inner, sizeof inner) == sizeof inner && inner == 0);
		CHECK(access(path, F_OK) == -1);
	}
	if (fails)
		return 1;
	puts("PASS");
	return 0;
}
