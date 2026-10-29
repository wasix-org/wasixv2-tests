// /proc/sys/fs/nr_open and /proc/sys/net/ipv4/conf as an unprivileged process sees them:
// Linux's defaults, root's files that only root opens for writing, a hard RLIMIT_NOFILE no
// higher than nr_open, and conf/ holding all, default and the loopback interface with
// devinet.c's defaults (loopback disables xfrm and policy for itself).
//#ExpectedStdout: nr_open: 1048576
//#ExpectedStdout: conf: all=1 default=1 lo=1
//#ExpectedStdout: lo: tag=0 disable_policy=1 igmpv2_unsolicited_report_interval=10000
//#ExpectedStdout: default: tag=0 disable_policy=0 accept_source_route=1
//#ExpectedStdout: all: accept_source_route=0 arp_evict_nocarrier=1
//#ExpectedStdout: ok
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#define CONF "/proc/sys/net/ipv4/conf/"

static int failures;
#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "line %d: %s (%s)\n", __LINE__, #cond, strerror(errno)); failures++; } } while (0)

/// The integer in the sysctl file `path`, or -1.
static long sysctl(const char *path)
{
	char text[32] = {0};
	int fd = open(path, O_RDONLY);
	if (fd < 0)
		return -1;
	ssize_t n = read(fd, text, sizeof text - 1);
	close(fd);
	if (n <= 0 || text[n - 1] != '\n')
		return -1;
	return strtol(text, NULL, 10);
}

/// The value of `name` in conf/`scope`.
static long conf(const char *scope, const char *name)
{
	char path[128];
	snprintf(path, sizeof path, CONF "%s/%s", scope, name);
	return sysctl(path);
}

/// Whether `path` is root's, with exactly `mode`.
static int roots(const char *path, mode_t mode)
{
	struct stat st;
	return stat(path, &st) == 0 && st.st_uid == 0 && (st.st_mode & 07777) == mode;
}

int main(void)
{
	long nr_open = sysctl("/proc/sys/fs/nr_open");
	printf("nr_open: %ld\n", nr_open);
	CHECK(roots("/proc/sys/fs/nr_open", 0644));
	errno = 0;
	CHECK(open("/proc/sys/fs/nr_open", O_WRONLY) == -1 && errno == EACCES);
	struct rlimit limit;
	CHECK(getrlimit(RLIMIT_NOFILE, &limit) == 0);
	limit.rlim_max = nr_open + 1;
	errno = 0;
	CHECK(setrlimit(RLIMIT_NOFILE, &limit) == -1 && errno == EPERM);

	int all = 0, dflt = 0, lo = 0;
	DIR *dir = opendir(CONF);
	CHECK(dir != NULL);
	for (struct dirent *e; dir && (e = readdir(dir));) {
		all |= strcmp(e->d_name, "all") == 0;
		dflt |= strcmp(e->d_name, "default") == 0;
		lo |= strcmp(e->d_name, "lo") == 0 && e->d_type == DT_DIR;
	}
	if (dir)
		closedir(dir);
	printf("conf: all=%d default=%d lo=%d\n", all, dflt, lo);
	printf("lo: tag=%ld disable_policy=%ld igmpv2_unsolicited_report_interval=%ld\n",
	       conf("lo", "tag"), conf("lo", "disable_policy"),
	       conf("lo", "igmpv2_unsolicited_report_interval"));
	printf("default: tag=%ld disable_policy=%ld accept_source_route=%ld\n",
	       conf("default", "tag"), conf("default", "disable_policy"),
	       conf("default", "accept_source_route"));
	printf("all: accept_source_route=%ld arp_evict_nocarrier=%ld\n",
	       conf("all", "accept_source_route"), conf("all", "arp_evict_nocarrier"));
	CHECK(roots(CONF "lo/tag", 0644));
	CHECK(roots(CONF "lo/mc_forwarding", 0444));
	errno = 0;
	CHECK(open(CONF "lo/tag", O_WRONLY) == -1 && errno == EACCES);
	errno = 0;
	CHECK(open(CONF "no-such-interface/tag", O_RDONLY) == -1 && errno == ENOENT);

	if (failures)
		return 1;
	puts("ok");
	return 0;
}
