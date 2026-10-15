// getifaddrs (libc asks the kernel over rtnetlink: RTM_GETLINK and RTM_GETADDR dumps): the
// loopback interface is up with 127.0.0.1/8 and ::1, and if_nametoindex agrees.
//#ExpectedStdout: lo up loopback
//#ExpectedStdout: lo inet 127.0.0.1 255.0.0.0
//#ExpectedStdout: lo inet6 ::1
//#ExpectedStdout: ok
#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#define CHECK(c)                                                                     \
	do {                                                                         \
		if (!(c)) {                                                          \
			printf("FAIL %s:%d: %s (errno=%d %s)\n", __FILE__, __LINE__, \
			       #c, errno, strerror(errno));                          \
			exit(1);                                                     \
		}                                                                    \
	} while (0)

int main(void)
{
	struct ifaddrs *list;
	CHECK(getifaddrs(&list) == 0);
	int link = 0, v4 = 0, v6 = 0;
	for (struct ifaddrs *i = list; i; i = i->ifa_next) {
		if (strcmp(i->ifa_name, "lo") != 0)
			continue;
		CHECK(i->ifa_flags & IFF_UP);
		CHECK(i->ifa_flags & IFF_LOOPBACK);
		if (!i->ifa_addr)
			continue;
		char text[INET6_ADDRSTRLEN], mask[INET6_ADDRSTRLEN];
		switch (i->ifa_addr->sa_family) {
		case AF_PACKET:
			if (!link++)
				printf("lo up loopback\n");
			break;
		case AF_INET:
			inet_ntop(AF_INET, &((struct sockaddr_in *)i->ifa_addr)->sin_addr, text,
				  sizeof text);
			CHECK(i->ifa_netmask != NULL);
			inet_ntop(AF_INET, &((struct sockaddr_in *)i->ifa_netmask)->sin_addr,
				  mask, sizeof mask);
			if (!v4++)
				printf("lo inet %s %s\n", text, mask);
			break;
		case AF_INET6:
			inet_ntop(AF_INET6, &((struct sockaddr_in6 *)i->ifa_addr)->sin6_addr, text,
				  sizeof text);
			if (!v6++)
				printf("lo inet6 %s\n", text);
			break;
		}
	}
	freeifaddrs(list);
	CHECK(link && v4 && v6);

	unsigned idx = if_nametoindex("lo");
	CHECK(idx != 0);
	char name[IF_NAMESIZE];
	CHECK(if_indextoname(idx, name) && strcmp(name, "lo") == 0);
	CHECK(if_nametoindex("does-not-exist") == 0);
	printf("ok\n");
	return 0;
}
