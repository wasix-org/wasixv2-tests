// Opening a pipe's /dev/fd or /proc/self/fd link never waits for the other side, unlike a FIFO
// (fs/pipe.c: fifo_open waits only off pipefs): a reader after the last writer gets what is
// buffered, then EOF, as `cat <(echo x)` needs; a writer after the last reader, blocking or
// not, opens and gets EPIPE instead of ENXIO.
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: CHECK(%s) failed (errno %s)\n", __LINE__, #c, strerror(errno)); fails++; } } while (0)
#define FAILS(call, e) do { errno = 0; long r_ = (long)(call); if (r_ != -1 || errno != (e)) { fprintf(stderr, "line %d: %s = %ld (%s), want -1 %s\n", __LINE__, #call, r_, strerror(errno), #e); fails++; } } while (0)

static int reopen(const char *dir, int fd, int flags)
{
	char path[64];
	snprintf(path, sizeof path, "%s/%d", dir, fd);
	return open(path, flags);
}

int main(void)
{
	signal(SIGPIPE, SIG_IGN);
	const char *dirs[] = { "/dev/fd", "/proc/self/fd" };
	for (int i = 0; i < 2; i++) {
		int p[2];
		char buf[16];
		CHECK(pipe(p) == 0);
		CHECK(write(p[1], "left", 4) == 4);
		CHECK(close(p[1]) == 0);
		int r = reopen(dirs[i], p[0], O_RDONLY);
		CHECK(r >= 0);
		CHECK(read(r, buf, sizeof buf) == 4 && !memcmp(buf, "left", 4));
		CHECK(read(r, buf, sizeof buf) == 0);
		close(r);
		close(p[0]);

		CHECK(pipe(p) == 0);
		CHECK(close(p[0]) == 0);
		int w = reopen(dirs[i], p[1], O_WRONLY);
		CHECK(w >= 0);
		FAILS(write(w, "x", 1), EPIPE);
		close(w);
		w = reopen(dirs[i], p[1], O_WRONLY | O_NONBLOCK);
		CHECK(w >= 0);
		FAILS(write(w, "x", 1), EPIPE);
		close(w);
		close(p[1]);
	}
	if (fails)
		return 1;
	puts("PASS");
	return 0;
}
