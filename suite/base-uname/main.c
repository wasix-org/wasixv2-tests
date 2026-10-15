// uname (wasixv2 reports system wasixv2 and machine wasm32), gethostname and getdomainname
// agreeing with it, sethostname needing privilege, and personality.
//#ExpectedStdout: ok
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/personality.h>
#include <sys/utsname.h>
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

int main(void)
{
	struct utsname u;
	memset(&u, 0xff, sizeof u);
	CHECK(uname(&u) == 0);
	CHECK(memchr(u.sysname, 0, sizeof u.sysname) && u.sysname[0]);
	CHECK(memchr(u.nodename, 0, sizeof u.nodename) && u.nodename[0]);
	CHECK(memchr(u.release, 0, sizeof u.release) && u.release[0]);
	CHECK(memchr(u.version, 0, sizeof u.version) && u.version[0]);
	CHECK(memchr(u.machine, 0, sizeof u.machine) && u.machine[0]);
#ifdef __wasixv2__
	CHECK(strcmp(u.sysname, "wasixv2") == 0);
	CHECK(strcmp(u.machine, "wasm32") == 0);
	CHECK(strcmp(u.release, "2.0.0") == 0);
#endif

	char host[256];
	CHECK(gethostname(host, sizeof host) == 0 && strcmp(host, u.nodename) == 0);
	char domain[256];
	CHECK(getdomainname(domain, sizeof domain) == 0 && strcmp(domain, u.domainname) == 0);

	errno = 0;
	CHECK(sethostname("other", 5) == -1 && errno == EPERM);
	errno = 0;
	CHECK(setdomainname("other", 5) == -1 && errno == EPERM);

	/* personality: query, set and restore. */
	int old = personality(0xffffffff);
	CHECK(old >= 0 && (old & PER_MASK) == PER_LINUX);
	CHECK(personality(old | ADDR_NO_RANDOMIZE) == old);
	CHECK(personality(0xffffffff) == (old | ADDR_NO_RANDOMIZE));
	CHECK(personality(old) == (old | ADDR_NO_RANDOMIZE));
	CHECK(personality(0xffffffff) == old);

	if (failures)
		return 1;
	puts("ok");
	return 0;
}
