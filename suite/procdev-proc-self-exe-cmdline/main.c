// /proc/self after execve: exe names the image's file, cmdline and environ hold the new
// arguments and environment NUL-separated, comm and stat name the task, and status agrees
// with getpid and getppid.
//#MappedDirectory: .:/t
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: CHECK(%s) failed (errno %s)\n", __LINE__, #c, strerror(errno)); fails++; } } while (0)

static size_t slurp(const char *path, char *buf, size_t size)
{
	int fd = open(path, O_RDONLY);
	if (fd < 0)
		return 0;
	size_t at = 0;
	ssize_t n;
	while (at < size && (n = read(fd, buf + at, size - at)) > 0)
		at += n;
	close(fd);
	return at;
}

static int child(void)
{
	char buf[4096];
	ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
	CHECK(n == 7 && !memcmp(buf, "/t/main", 7));

	static const char cmdline[] = "/t/main\0child\0two words";
	size_t len = slurp("/proc/self/cmdline", buf, sizeof buf);
	CHECK(len == sizeof cmdline && !memcmp(buf, cmdline, sizeof cmdline));

	static const char environ_[] = "A=1\0B=two";
	len = slurp("/proc/self/environ", buf, sizeof buf);
	CHECK(len == sizeof environ_ && !memcmp(buf, environ_, sizeof environ_));

	len = slurp("/proc/self/comm", buf, sizeof buf);
	CHECK(len == 5 && !memcmp(buf, "main\n", 5));

	len = slurp("/proc/self/stat", buf, sizeof buf - 1);
	buf[len] = 0;
	char expect[64];
	snprintf(expect, sizeof expect, "%d (main) ", getpid());
	CHECK(!strncmp(buf, expect, strlen(expect)));

	len = slurp("/proc/self/status", buf, sizeof buf - 1);
	buf[len] = 0;
	snprintf(expect, sizeof expect, "\nPid:\t%d\n", getpid());
	CHECK(strstr(buf, expect) != NULL);
	snprintf(expect, sizeof expect, "\nPPid:\t%d\n", getppid());
	CHECK(strstr(buf, expect) != NULL);
	CHECK(!strncmp(buf, "Name:\tmain\n", 11));

	/* /proc/<pid> is the same directory as /proc/self. */
	char path[64];
	snprintf(path, sizeof path, "/proc/%d/exe", getpid());
	n = readlink(path, buf, sizeof buf - 1);
	CHECK(n == 7 && !memcmp(buf, "/t/main", 7));
	n = readlink("/proc/self", buf, sizeof buf - 1);
	buf[n > 0 ? n : 0] = 0;
	CHECK(atoi(buf) == getpid());

	if (fails)
		return 1;
	puts("PASS");
	return 0;
}

int main(int argc, char **argv)
{
	if (argc > 1 && !strcmp(argv[1], "child"))
		return child();
	char *args[] = {"/t/main", "child", "two words", NULL};
	char *env[] = {"A=1", "B=two", NULL};
	execve("/t/main", args, env);
	perror("execve");
	return 1;
}
