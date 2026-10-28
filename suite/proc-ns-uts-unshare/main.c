// unshare(CLONE_NEWUTS): a new UTS namespace starts with a copy of the names, which sethostname
// then changes for it alone; uname and /proc/sys/kernel/hostname show the caller's namespace,
// /proc/self/ns/uts names the new one, and setns back into the old one finds its names as they
// were. Without CAP_SYS_ADMIN (a uid other than 0), unshare and sethostname fail with EPERM.
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: CHECK(%s) failed (errno %s)\n", __LINE__, #c, strerror(errno)); fails++; } } while (0)
#define FAILS(call, e) do { errno = 0; long r_ = (long)(call); if (r_ != -1 || errno != (e)) { fprintf(stderr, "line %d: %s = %ld (%s), want -1 %s\n", __LINE__, #call, r_, strerror(errno), #e); fails++; } } while (0)

/// /proc/sys/kernel/hostname without its newline.
static const char *proc_hostname(void)
{
	static char buf[128];
	buf[0] = 0;
	int fd = open("/proc/sys/kernel/hostname", O_RDONLY);
	if (fd < 0)
		return "(unreadable)";
	ssize_t n = read(fd, buf, sizeof buf - 1);
	close(fd);
	buf[n > 0 ? n : 0] = 0;
	buf[strcspn(buf, "\n")] = 0;
	return buf;
}

static const char *nodename(void)
{
	static struct utsname u;
	return uname(&u) == 0 ? u.nodename : "(uname failed)";
}

static void link_of(const char *path, char *out, size_t size)
{
	ssize_t n = readlink(path, out, size - 1);
	out[n > 0 ? n : 0] = 0;
}

int main(void)
{
	char before[64], after[64], original[65];
	snprintf(original, sizeof original, "%s", nodename());
	CHECK(strcmp(proc_hostname(), original) == 0);
	link_of("/proc/self/ns/uts", before, sizeof before);

	if (geteuid() != 0) {
		FAILS(unshare(CLONE_NEWUTS), EPERM);
		FAILS(sethostname("x", 1), EPERM);
		link_of("/proc/self/ns/uts", after, sizeof after);
		CHECK(strcmp(before, after) == 0);
	} else {
		int old = open("/proc/self/ns/uts", O_RDONLY);
		CHECK(old >= 0);
		CHECK(unshare(CLONE_NEWUTS) == 0);
		link_of("/proc/self/ns/uts", after, sizeof after);
		CHECK(strncmp(after, "uts:[", 5) == 0 && strcmp(before, after) != 0);
		CHECK(strcmp(nodename(), original) == 0);

		CHECK(sethostname("inside", 6) == 0);
		CHECK(strcmp(nodename(), "inside") == 0);
		CHECK(strcmp(proc_hostname(), "inside") == 0);

		CHECK(setns(old, CLONE_NEWUTS) == 0);
		link_of("/proc/self/ns/uts", after, sizeof after);
		CHECK(strcmp(before, after) == 0);
		CHECK(strcmp(nodename(), original) == 0);
		CHECK(strcmp(proc_hostname(), original) == 0);
	}

	if (fails)
		return 1;
	puts("PASS");
	return 0;
}
