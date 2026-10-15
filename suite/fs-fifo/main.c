// FIFOs: mkfifo, non-blocking opens (ENXIO for a writer without readers), data through the
// pipe, EOF once the writer closes, ESPIPE.
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: CHECK(%s) failed (errno %s)\n", __LINE__, #c, strerror(errno)); fails++; } } while (0)
#define OK(call) do { errno = 0; if ((long)(call) < 0) { fprintf(stderr, "line %d: %s failed: %s\n", __LINE__, #call, strerror(errno)); fails++; } } while (0)
#define FAILS(call, e) do { errno = 0; long r_ = (long)(call); if (r_ != -1 || errno != (e)) { fprintf(stderr, "line %d: %s = %ld (%s), want -1 %s\n", __LINE__, #call, r_, strerror(errno), #e); fails++; } } while (0)

int main(void)
{
	struct stat st;
	char buf[16];

	OK(mkfifo("/tmp/p", 0644));
	OK(stat("/tmp/p", &st));
	CHECK(S_ISFIFO(st.st_mode));
	FAILS(mkfifo("/tmp/p", 0644), EEXIST);

	FAILS(open("/tmp/p", O_WRONLY | O_NONBLOCK), ENXIO);
	int r = open("/tmp/p", O_RDONLY | O_NONBLOCK);
	CHECK(r >= 0);
	/* No writer has ever opened it: no data and no EOF yet? Linux returns 0 here. */
	CHECK(read(r, buf, sizeof buf) == 0);
	int w = open("/tmp/p", O_WRONLY | O_NONBLOCK);
	CHECK(w >= 0);
	FAILS(read(r, buf, sizeof buf), EAGAIN);
	CHECK(write(w, "hey", 3) == 3);
	struct pollfd pfd = { .fd = r, .events = POLLIN };
	CHECK(poll(&pfd, 1, 0) == 1 && (pfd.revents & POLLIN));
	CHECK(read(r, buf, sizeof buf) == 3 && !memcmp(buf, "hey", 3));
	FAILS(lseek(r, 0, SEEK_CUR), ESPIPE);
	OK(fstat(r, &st));
	CHECK(S_ISFIFO(st.st_mode));

	/* A second writer on the same FIFO shares the pipe. */
	int w2 = open("/tmp/p", O_WRONLY | O_NONBLOCK);
	CHECK(write(w2, "ab", 2) == 2);
	close(w);
	close(w2);
	CHECK(read(r, buf, sizeof buf) == 2);
	CHECK(read(r, buf, sizeof buf) == 0);
	close(r);

	/* O_RDWR never blocks and holds both ends. */
	int rw = open("/tmp/p", O_RDWR);
	CHECK(rw >= 0);
	CHECK(write(rw, "x", 1) == 1);
	CHECK(read(rw, buf, 1) == 1 && buf[0] == 'x');
	close(rw);

	OK(mknod("/tmp/p2", S_IFIFO | 0600, 0));
	OK(stat("/tmp/p2", &st));
	CHECK(S_ISFIFO(st.st_mode));
	OK(unlink("/tmp/p2"));

	if (fails)
		return 1;
	puts("PASS");
	return 0;
}
