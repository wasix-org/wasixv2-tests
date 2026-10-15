// TCP over IPv6 loopback, and a dual-stack listener on [::] accepting an IPv4 client, which it
// sees as an IPv4-mapped address.
//#ExpectedStdout: ok
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

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
	int srv = socket(AF_INET6, SOCK_STREAM, 0);
	CHECK(srv >= 0);
	int v6only = 1;
	socklen_t len = sizeof v6only;
	CHECK(getsockopt(srv, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, &len) == 0 && v6only == 0);

	struct sockaddr_in6 addr = { .sin6_family = AF_INET6, .sin6_addr = IN6ADDR_ANY_INIT };
	CHECK(bind(srv, (struct sockaddr *)&addr, sizeof addr) == 0);
	len = sizeof addr;
	CHECK(getsockname(srv, (struct sockaddr *)&addr, &len) == 0 && len == sizeof addr);
	CHECK(addr.sin6_family == AF_INET6 && addr.sin6_port != 0);
	CHECK(listen(srv, 4) == 0);

	// An IPv6 client to ::1.
	int c6 = socket(AF_INET6, SOCK_STREAM, 0);
	CHECK(c6 >= 0);
	struct sockaddr_in6 to6 = addr;
	to6.sin6_addr = in6addr_loopback;
	CHECK(connect(c6, (struct sockaddr *)&to6, sizeof to6) == 0);
	struct sockaddr_in6 peer;
	len = sizeof peer;
	int a6 = accept(srv, (struct sockaddr *)&peer, &len);
	CHECK(a6 >= 0);
	CHECK(peer.sin6_family == AF_INET6 && IN6_IS_ADDR_LOOPBACK(&peer.sin6_addr));
	CHECK(write(c6, "six", 3) == 3);
	char buf[8];
	CHECK(read(a6, buf, sizeof buf) == 3 && memcmp(buf, "six", 3) == 0);

	// An IPv4 client reaches the dual-stack listener.
	int c4 = socket(AF_INET, SOCK_STREAM, 0);
	CHECK(c4 >= 0);
	struct sockaddr_in to4 = { .sin_family = AF_INET, .sin_port = addr.sin6_port };
	to4.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	CHECK(connect(c4, (struct sockaddr *)&to4, sizeof to4) == 0);
	len = sizeof peer;
	int a4 = accept(srv, (struct sockaddr *)&peer, &len);
	CHECK(a4 >= 0);
	CHECK(peer.sin6_family == AF_INET6 && IN6_IS_ADDR_V4MAPPED(&peer.sin6_addr));
	char text[INET6_ADDRSTRLEN];
	CHECK(inet_ntop(AF_INET6, &peer.sin6_addr, text, sizeof text) != NULL);
	CHECK(strcmp(text, "::ffff:127.0.0.1") == 0);

	// A v6-only socket cannot take the port the dual-stack one holds for IPv4 too, but a
	// separate IPv4 socket conflicts with it.
	int s4 = socket(AF_INET, SOCK_STREAM, 0);
	CHECK(s4 >= 0);
	CHECK(bind(s4, (struct sockaddr *)&to4, sizeof to4) == -1 && errno == EADDRINUSE);

	close(a4);
	close(c4);
	close(a6);
	close(c6);
	close(s4);
	close(srv);
	printf("ok\n");
	return 0;
}
