// inotify: watches on a directory and a file, the events for create/modify/attrib/move/
// delete, IN_IGNORED on removal of a watch, non-blocking reads and argument errors.
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: CHECK(%s) failed (errno %s)\n", __LINE__, #c, strerror(errno)); fails++; } } while (0)
#define OK(call) do { errno = 0; if ((long)(call) < 0) { fprintf(stderr, "line %d: %s failed: %s\n", __LINE__, #call, strerror(errno)); fails++; } } while (0)
#define FAILS(call, e) do { errno = 0; long r_ = (long)(call); if (r_ != -1 || errno != (e)) { fprintf(stderr, "line %d: %s = %ld (%s), want -1 %s\n", __LINE__, #call, r_, strerror(errno), #e); fails++; } } while (0)

static char evbuf[4096] __attribute__((aligned(8)));
static int nev;
static struct {
	int wd;
	unsigned mask, cookie;
	char name[32];
} ev[32];

static void drain(int in)
{
	nev = 0;
	ssize_t n = read(in, evbuf, sizeof evbuf);
	for (char *p = evbuf; n > 0 && p < evbuf + n;) {
		struct inotify_event *e = (struct inotify_event *)p;
		if (nev < 32) {
			ev[nev].wd = e->wd;
			ev[nev].mask = e->mask;
			ev[nev].cookie = e->cookie;
			snprintf(ev[nev].name, sizeof ev[nev].name, "%s", e->len ? e->name : "");
			nev++;
		}
		p += sizeof *e + e->len;
	}
}

static int has(int wd, unsigned mask, const char *name)
{
	for (int i = 0; i < nev; i++)
		if (ev[i].wd == wd && (ev[i].mask & mask) && !strcmp(ev[i].name, name))
			return 1;
	return 0;
}

int main(void)
{
	OK(mkdir("/tmp/d", 0755));
	int in = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	CHECK(in >= 0);
	CHECK(fcntl(in, F_GETFD) == FD_CLOEXEC);
	char tmp[4];
	FAILS(read(in, tmp, sizeof tmp), EAGAIN);

	int wd = inotify_add_watch(in, "/tmp/d", IN_ALL_EVENTS);
	CHECK(wd >= 1);
	CHECK(inotify_add_watch(in, "/tmp/d", IN_CREATE) == wd);
	OK(inotify_add_watch(in, "/tmp/d", IN_ALL_EVENTS));

	int fd = open("/tmp/d/f", O_CREAT | O_WRONLY, 0644);
	CHECK(write(fd, "x", 1) == 1);
	close(fd);
	struct pollfd pfd = { .fd = in, .events = POLLIN };
	CHECK(poll(&pfd, 1, 0) == 1);
	drain(in);
	CHECK(has(wd, IN_CREATE, "f"));
	CHECK(has(wd, IN_OPEN, "f"));
	CHECK(has(wd, IN_MODIFY, "f"));
	CHECK(has(wd, IN_CLOSE_WRITE, "f"));

	int fwd = inotify_add_watch(in, "/tmp/d/f", IN_ATTRIB | IN_DELETE_SELF);
	CHECK(fwd >= 1 && fwd != wd);
	OK(chmod("/tmp/d/f", 0600));
	drain(in);
	CHECK(has(wd, IN_ATTRIB, "f"));
	CHECK(has(fwd, IN_ATTRIB, ""));

	OK(rename("/tmp/d/f", "/tmp/d/g"));
	drain(in);
	CHECK(has(wd, IN_MOVED_FROM, "f") && has(wd, IN_MOVED_TO, "g"));
	CHECK(nev >= 2 && ev[0].cookie != 0 && ev[0].cookie == ev[1].cookie);

	OK(unlink("/tmp/d/g"));
	drain(in);
	CHECK(has(wd, IN_DELETE, "g"));
	CHECK(has(fwd, IN_DELETE_SELF, ""));
	CHECK(has(fwd, IN_IGNORED, ""));

	OK(mkdir("/tmp/d/sub", 0755));
	drain(in);
	CHECK(has(wd, IN_CREATE | IN_ISDIR, "sub"));
	for (int i = 0; i < nev; i++)
		if (!strcmp(ev[i].name, "sub"))
			CHECK(ev[i].mask & IN_ISDIR);

	OK(inotify_rm_watch(in, wd));
	drain(in);
	CHECK(has(wd, IN_IGNORED, ""));
	FAILS(inotify_rm_watch(in, wd), EINVAL);
	FAILS(inotify_add_watch(in, "/tmp/none", IN_ALL_EVENTS), ENOENT);
	FAILS(inotify_add_watch(in, "/tmp/d", 0), EINVAL);
	FAILS(inotify_add_watch(1, "/tmp/d", IN_ALL_EVENTS), EINVAL);
	CHECK(inotify_add_watch(in, "/tmp/d/sub", IN_ONLYDIR | IN_CREATE) >= 1);
	close(open("/tmp/d/file", O_CREAT | O_WRONLY, 0644));
	FAILS(inotify_add_watch(in, "/tmp/d/file", IN_ONLYDIR | IN_CREATE), ENOTDIR);
	close(in);

	if (fails)
		return 1;
	puts("PASS");
	return 0;
}
