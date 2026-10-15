// Process startup: argv, envp and environ, getenv/setenv/unsetenv/putenv/clearenv, and the
// auxiliary vector (page size, clock ticks, ids, AT_RANDOM, AT_SECURE).
//#Args: alpha beta-gamma 3
//#Env: FOO=bar
//#Env: EMPTY=
//#Env: EQ=a=b
//#ExpectedStdout: ok
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/auxv.h>
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

extern char **environ;

static int in_environ(const char *entry)
{
	for (char **e = environ; *e; e++)
		if (strcmp(*e, entry) == 0)
			return 1;
	return 0;
}

int main(int argc, char **argv, char **envp)
{
	CHECK(argc == 4);
	CHECK(argv[0] != NULL && argv[0][0] != 0);
	CHECK(argc >= 4 && strcmp(argv[1], "alpha") == 0 && strcmp(argv[2], "beta-gamma") == 0 &&
	      strcmp(argv[3], "3") == 0);
	CHECK(argv[argc] == NULL);
	CHECK(envp == environ);
	CHECK(program_invocation_short_name && program_invocation_short_name[0]);

	CHECK(getenv("FOO") && strcmp(getenv("FOO"), "bar") == 0);
	CHECK(getenv("EMPTY") && strcmp(getenv("EMPTY"), "") == 0);
	CHECK(getenv("EQ") && strcmp(getenv("EQ"), "a=b") == 0);
	CHECK(getenv("NOT_SET") == NULL);
	CHECK(in_environ("FOO=bar") && in_environ("EQ=a=b"));

	CHECK(setenv("NEW", "value", 0) == 0 && strcmp(getenv("NEW"), "value") == 0);
	CHECK(setenv("NEW", "other", 0) == 0 && strcmp(getenv("NEW"), "value") == 0);
	CHECK(setenv("NEW", "other", 1) == 0 && strcmp(getenv("NEW"), "other") == 0);
	CHECK(in_environ("NEW=other"));
	errno = 0;
	CHECK(setenv("BAD=NAME", "x", 1) == -1 && errno == EINVAL);
	CHECK(unsetenv("FOO") == 0 && getenv("FOO") == NULL && !in_environ("FOO=bar"));
	static char put[] = "PUT=it";
	CHECK(putenv(put) == 0 && getenv("PUT") == put + 4);
	CHECK(clearenv() == 0 && (environ == NULL || environ[0] == NULL));
	CHECK(getenv("EQ") == NULL);

	long ps = sysconf(_SC_PAGESIZE);
	CHECK(getauxval(AT_PAGESZ) == (unsigned long)ps);
#ifdef __wasixv2__
	CHECK(ps == 65536);
#endif
	CHECK(getauxval(AT_CLKTCK) == (unsigned long)sysconf(_SC_CLK_TCK));
	CHECK(getauxval(AT_UID) == getuid() && getauxval(AT_EUID) == geteuid());
	CHECK(getauxval(AT_GID) == getgid() && getauxval(AT_EGID) == getegid());
	CHECK(getauxval(AT_SECURE) == 0);
	const unsigned char *random = (const unsigned char *)getauxval(AT_RANDOM);
	CHECK(random != NULL);
	if (random) {
		int nonzero = 0;
		for (int i = 0; i < 16; i++)
			nonzero |= random[i];
		CHECK(nonzero);
	}
	errno = 0;
	CHECK(getauxval(12345) == 0 && errno == ENOENT);

	if (failures)
		return 1;
	puts("ok");
	return 0;
}
