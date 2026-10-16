// The memory and random devices (drivers/char/mem.c, drivers/char/random.c): device numbers
// and modes, /dev/null at end of file and swallowing writes, /dev/zero reading zeroes,
// /dev/full refusing writes with ENOSPC, /dev/urandom and /dev/random reading at once, seeks
// and positional reads, poll, and /dev/tty without a controlling terminal.
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: CHECK(%s) failed (errno %s)\n", __LINE__, #c, strerror(errno)); fails++; } } while (0)
#define FAILS(call, e) do { errno = 0; long r_ = (long)(call); if (r_ != -1 || errno != (e)) { fprintf(stderr, "line %d: %s = %ld (%s), want -1 %s\n", __LINE__, #call, r_, strerror(errno), #e); fails++; } } while (0)

static void check_node(const char *path, unsigned major_, unsigned minor_)
{
	struct stat st;
	CHECK(stat(path, &st) == 0);
	CHECK(S_ISCHR(st.st_mode));
	CHECK((st.st_mode & 07777) == 0666);
	CHECK(major(st.st_rdev) == major_ && minor(st.st_rdev) == minor_);
	CHECK(st.st_uid == 0);
}

int main(void)
{
	char buf[64];
	check_node("/dev/null", 1, 3);
	check_node("/dev/zero", 1, 5);
	check_node("/dev/full", 1, 7);
	check_node("/dev/random", 1, 8);
	check_node("/dev/urandom", 1, 9);
	check_node("/dev/tty", 5, 0);

	int null = open("/dev/null", O_RDWR);
	CHECK(null >= 0);
	CHECK(read(null, buf, sizeof buf) == 0);
	CHECK(write(null, buf, sizeof buf) == sizeof buf);
	CHECK(lseek(null, 100, SEEK_SET) == 0);
	CHECK(pread(null, buf, sizeof buf, 10) == 0);
	struct pollfd pfd = { .fd = null, .events = POLLIN | POLLOUT };
	CHECK(poll(&pfd, 1, 0) == 1 && (pfd.revents & (POLLIN | POLLOUT)) == (POLLIN | POLLOUT));

	int zero = open("/dev/zero", O_RDONLY);
	CHECK(zero >= 0);
	memset(buf, 'x', sizeof buf);
	CHECK(read(zero, buf, sizeof buf) == sizeof buf);
	static const char zeroes[sizeof buf];
	CHECK(!memcmp(buf, zeroes, sizeof buf));
	FAILS(write(zero, buf, 1), EBADF);

	int full = open("/dev/full", O_RDWR);
	CHECK(full >= 0);
	FAILS(write(full, "x", 1), ENOSPC);
	CHECK(read(full, buf, 8) == 8 && !memcmp(buf, zeroes, 8));

	char a[32], b[32];
	int ur = open("/dev/urandom", O_RDONLY);
	CHECK(ur >= 0);
	CHECK(read(ur, a, sizeof a) == sizeof a);
	CHECK(read(ur, b, sizeof b) == sizeof b);
	CHECK(memcmp(a, b, sizeof a) != 0);
	int r = open("/dev/random", O_RDONLY | O_NONBLOCK);
	CHECK(r >= 0);
	CHECK(read(r, a, sizeof a) == sizeof a);
	CHECK(write(r, "seed", 4) == -1 && errno == EBADF);

	FAILS(open("/dev/tty", O_RDWR), ENXIO);

	if (fails)
		return 1;
	puts("PASS");
	return 0;
}
