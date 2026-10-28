// The interface requests (SIOCGIFINDEX, SIOCGIFFLAGS) answer for the network namespace a socket
// was made in (net/socket.c: sock_ioctl passes sock_net(sk)), not the caller's: after
// unshare(CLONE_NEWNET), a socket made before still finds every interface of the old namespace
// at its index, its loopback up, and a socket made after finds none of the old namespace's
// other interfaces. Without CAP_SYS_ADMIN (a uid other than 0), unshare fails with EPERM.
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <net/if.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: CHECK(%s) failed (errno %s)\n", __LINE__, #c, strerror(errno)); fails++; } } while (0)
#define FAILS(call, e) do { errno = 0; long r_ = (long)(call); if (r_ != -1 || errno != (e)) { fprintf(stderr, "line %d: %s = %ld (%s), want -1 %s\n", __LINE__, #call, r_, strerror(errno), #e); fails++; } } while (0)

/// SIOCGIFINDEX of `name` through `fd`, or -1.
static int index_of(int fd, const char *name)
{
	struct ifreq ifr = {0};
	strncpy(ifr.ifr_name, name, IFNAMSIZ - 1);
	return ioctl(fd, SIOCGIFINDEX, &ifr) == 0 ? ifr.ifr_ifindex : -1;
}

static int up(int fd, const char *name)
{
	struct ifreq ifr = {0};
	strncpy(ifr.ifr_name, name, IFNAMSIZ - 1);
	return ioctl(fd, SIOCGIFFLAGS, &ifr) == 0 && (ifr.ifr_flags & IFF_UP);
}

static void in_a_new_namespace(void)
{
	struct if_nameindex *old = if_nameindex();
	CHECK(old != NULL);
	int before = socket(AF_UNIX, SOCK_DGRAM, 0);
	CHECK(before >= 0 && up(before, "lo"));

	CHECK(unshare(CLONE_NEWNET) == 0);

	int after = socket(AF_UNIX, SOCK_DGRAM, 0);
	CHECK(after >= 0);
	for (struct if_nameindex *i = old; i && i->if_index; i++) {
		CHECK(index_of(before, i->if_name) == (int)i->if_index);
		if (strcmp(i->if_name, "lo") != 0)
			FAILS(index_of(after, i->if_name), ENODEV);
	}
	CHECK(up(before, "lo"));
	if_freenameindex(old);
}

int main(void)
{
	int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
	CHECK(index_of(fd, "lo") > 0);
	FAILS(index_of(fd, "no-such-if0"), ENODEV);
	if (geteuid() != 0)
		FAILS(unshare(CLONE_NEWNET), EPERM);
	else
		in_a_new_namespace();
	if (fails)
		return 1;
	puts("PASS");
	return 0;
}
