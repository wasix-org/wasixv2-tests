// getrandom with each flag combination, large and empty requests, and getentropy's limit.
//#ExpectedStdout: ok
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <sys/syscall.h>
#include <unistd.h>

static int failures;
#define CHECK(cond)                                                                       \
	do {                                                                              \
		if (!(cond)) {                                                            \
			printf("FAIL line %d: %s (errno %d: %s)\n", __LINE__, #cond, errno, \
			       strerror(errno));                                          \
			failures++;                                                       \
		}                                                                         \
	} while (0)

#ifndef GRND_INSECURE
#define GRND_INSECURE 4
#endif

static int nonzero(const unsigned char *p, size_t len)
{
	for (size_t i = 0; i < len; i++)
		if (p[i])
			return 1;
	return 0;
}

int main(void)
{
	unsigned char a[64] = {0}, b[64] = {0};
	CHECK(getrandom(a, sizeof a, 0) == sizeof a);
	CHECK(getrandom(b, sizeof b, 0) == sizeof b);
	CHECK(nonzero(a, sizeof a) && memcmp(a, b, sizeof a) != 0);

	unsigned flags[] = {GRND_NONBLOCK, GRND_RANDOM, GRND_RANDOM | GRND_NONBLOCK, GRND_INSECURE,
			    GRND_INSECURE | GRND_NONBLOCK};
	for (size_t i = 0; i < sizeof flags / sizeof flags[0]; i++) {
		memset(a, 0, sizeof a);
		ssize_t n = getrandom(a, sizeof a, flags[i]);
		if (n != sizeof a || !nonzero(a, sizeof a))
			printf("FAIL getrandom flags %#x = %zd errno %d\n", flags[i], n, errno),
				failures++;
	}

	CHECK(getrandom(a, 0, 0) == 0);

	size_t big = 1 << 20;
	unsigned char *p = malloc(big);
	ssize_t n = getrandom(p, big, 0);
	CHECK(n > 0 && (size_t)n <= big && nonzero(p, n));
	free(p);

	errno = 0;
	CHECK(syscall(SYS_getrandom, a, sizeof a, 0x100) == -1 && errno == EINVAL);
	errno = 0;
	CHECK(syscall(SYS_getrandom, a, sizeof a, GRND_INSECURE | GRND_RANDOM) == -1 && errno == EINVAL);
	errno = 0;
	CHECK(syscall(SYS_getrandom, NULL, 16, 0) == -1 && errno == EFAULT);

	unsigned char e[256];
	CHECK(getentropy(e, sizeof e) == 0 && nonzero(e, sizeof e));
	errno = 0;
	CHECK(getentropy(e, 257) == -1 && errno == EIO);

	if (failures)
		return 1;
	puts("ok");
	return 0;
}
