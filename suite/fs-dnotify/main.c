// dnotify (F_NOTIFY): a directory's open file registered for DN_* events sends its F_SETSIG
// signal with POLL_MSG and the descriptor in si_fd when they happen, once, or for good with
// DN_MULTISHOT; an empty mask ends it, as does closing any descriptor of the file. DN_RENAME is
// a rename within the directory, a move out is a DN_DELETE and a move in a DN_CREATE; a change
// to a subdirectory reaches its own registration and its parent's, a write to a file in the
// directory is a DN_MODIFY. F_NOTIFY on a file is ENOTDIR.
//#ExpectedStdout: create 1 1
//#ExpectedStdout: oneshot 0
//#ExpectedStdout: multishot 2
//#ExpectedStdout: removed 0
//#ExpectedStdout: rename across 0 0
//#ExpectedStdout: rename within 1
//#ExpectedStdout: move out and in 1 1
//#ExpectedStdout: attrib 1 1
//#ExpectedStdout: modify 1
//#ExpectedStdout: closed 0
//#ExpectedStdout: file ENOTDIR
//#ExpectedExitCode: 0
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int rt;
static char dir[64];
static int code_ok = 1;
static int d, s;
/// The signals taken so far that named `d` and `s`.
static int got_d, got_s;

/// Takes the queued realtime signals, counting those that name `d` and `s`; all must say
/// POLL_MSG.
static void take(void)
{
	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, rt);
	struct timespec now = { 0, 0 };
	siginfo_t si;
	got_d = got_s = 0;
	while (sigtimedwait(&set, &si, &now) == rt) {
		if (si.si_code != POLL_MSG)
			code_ok = 0;
		got_d += si.si_fd == d;
		got_s += si.si_fd == s;
	}
}

static const char *at(const char *name)
{
	static char path[2][128];
	static int turn;
	turn ^= 1;
	snprintf(path[turn], sizeof path[turn], "%s/%s", dir, name);
	return path[turn];
}

static void touch(const char *name)
{
	close(open(at(name), O_CREAT | O_WRONLY, 0644));
}

static void notify(int fd, int arg)
{
	if (fcntl(fd, F_NOTIFY, arg))
		perror("F_NOTIFY");
}

int main(void)
{
	rt = SIGRTMIN;
	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, rt);
	sigprocmask(SIG_BLOCK, &set, 0);
	snprintf(dir, sizeof dir, "/tmp/dnotifyXXXXXX");
	if (!mkdtemp(dir) || mkdir(at("sub"), 0755))
		return 1;
	d = open(dir, O_RDONLY);
	s = open(at("sub"), O_RDONLY);
	fcntl(d, F_SETSIG, rt);
	fcntl(s, F_SETSIG, rt);

	notify(d, DN_CREATE);
	touch("a");
	take();
	printf("create %d %d\n", got_d, code_ok);
	touch("b");
	take();
	printf("oneshot %d\n", got_d);
	notify(d, DN_CREATE | DN_MULTISHOT);
	touch("c");
	touch("e");
	take();
	printf("multishot %d\n", got_d);
	notify(d, DN_MULTISHOT);
	touch("g");
	take();
	printf("removed %d\n", got_d);

	notify(d, DN_RENAME | DN_MULTISHOT);
	notify(s, DN_RENAME | DN_MULTISHOT);
	rename(at("a"), at("sub/a"));
	take();
	printf("rename across %d %d\n", got_d, got_s);
	rename(at("b"), at("b2"));
	take();
	printf("rename within %d\n", got_d);
	notify(d, 0);
	notify(s, 0);

	notify(d, DN_DELETE | DN_MULTISHOT);
	notify(s, DN_CREATE | DN_MULTISHOT);
	rename(at("c"), at("sub/c"));
	take();
	printf("move out and in %d %d\n", got_d, got_s);
	notify(d, 0);
	notify(s, 0);

	notify(d, DN_ATTRIB | DN_MULTISHOT);
	notify(s, DN_ATTRIB | DN_MULTISHOT);
	chmod(at("sub"), 0700);
	take();
	printf("attrib %d %d\n", got_d, got_s);
	notify(s, 0);

	notify(d, DN_MODIFY | DN_MULTISHOT);
	int e = open(at("e"), O_WRONLY);
	if (write(e, "x", 1) != 1)
		return 1;
	take();
	printf("modify %d\n", got_d);
	close(dup(d));
	if (write(e, "x", 1) != 1)
		return 1;
	take();
	printf("closed %d\n", got_d);

	errno = 0;
	fcntl(e, F_NOTIFY, DN_CREATE);
	printf("file %s\n", errno == ENOTDIR ? "ENOTDIR" : "?");
	return 0;
}
