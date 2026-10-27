// /dev/zero and /dev/urandom fill a read of any size (drivers/char/mem.c read_iter_zero,
// drivers/char/random.c urandom_read_iter), as `dd bs=1M` expects; readv across buffers too.
// A pipe read still returns what the pipe has.
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>
#include <unistd.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: CHECK(%s) failed (errno %s)\n", __LINE__, #c, strerror(errno)); fails++; } } while (0)

#define LEN (4 << 20)

int main(void)
{
	char *buf = malloc(LEN);
	CHECK(buf != NULL);
	int zero = open("/dev/zero", O_RDONLY);
	CHECK(zero >= 0);
	memset(buf, 'x', LEN);
	CHECK(read(zero, buf, LEN) == LEN);
	int all_zero = 1;
	for (size_t i = 0; i < LEN; i++)
		all_zero &= buf[i] == 0;
	CHECK(all_zero);
	CHECK(pread(zero, buf, LEN, 12345) == LEN);
	struct iovec iov[2] = { { buf, 300000 }, { buf + 300000, 900000 } };
	CHECK(readv(zero, iov, 2) == 1200000);

	int urandom = open("/dev/urandom", O_RDONLY);
	CHECK(urandom >= 0);
	CHECK(read(urandom, buf, LEN) == LEN);
	int random = open("/dev/random", O_RDONLY);
	CHECK(random >= 0);
	CHECK(read(random, buf, 1 << 20) == 1 << 20);

	int p[2];
	CHECK(pipe(p) == 0);
	CHECK(write(p[1], "abc", 3) == 3);
	CHECK(read(p[0], buf, LEN) == 3);
	if (fails)
		return 1;
	puts("PASS");
	return 0;
}
