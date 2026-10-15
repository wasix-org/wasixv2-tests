// sysinfo and the sysconf values libc derives from it and from the CPU affinity mask: memory
// and CPU counts, uptime, process count.
//#ExpectedStdout: ok
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/sysinfo.h>
#include <time.h>
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
	struct sysinfo si;
	memset(&si, 0xff, sizeof si);
	CHECK(sysinfo(&si) == 0);
	CHECK(si.uptime >= 0);
	CHECK(si.mem_unit >= 1);
	CHECK(si.totalram > 0);
	CHECK(si.freeram <= si.totalram);
	CHECK(si.freeram > 0);
	CHECK(si.sharedram <= si.totalram && si.bufferram <= si.totalram);
	CHECK(si.freeswap <= si.totalswap);
	CHECK(si.freehigh <= si.totalhigh);
	CHECK(si.procs >= 1);

	/* Uptime agrees with CLOCK_BOOTTIME. */
	struct timespec boot;
	CHECK(clock_gettime(CLOCK_BOOTTIME, &boot) == 0);
	CHECK(boot.tv_sec - si.uptime >= -1 && boot.tv_sec - si.uptime <= 1);

	long pages = sysconf(_SC_PHYS_PAGES);
	long avail = sysconf(_SC_AVPHYS_PAGES);
	CHECK(pages > 0 && avail > 0 && avail <= pages);
	long online = sysconf(_SC_NPROCESSORS_ONLN);
	long conf = sysconf(_SC_NPROCESSORS_CONF);
	CHECK(online >= 1 && conf >= online);
	CHECK(get_nprocs() == online);
	CHECK(get_nprocs_conf() >= 1);
	CHECK(get_phys_pages() == pages);

	errno = 0;
	CHECK(sysinfo(NULL) == -1 && errno == EFAULT);

	if (failures)
		return 1;
	puts("ok");
	return 0;
}
