// IP_BIND_ADDRESS_NO_PORT (__inet_bind, __inet6_bind): bind with port 0 keeps the address and
// takes no port (getsockname shows port 0, a second bind is allowed); connect, listen or a
// send then take a port that goes with that address. A disconnected UDP socket keeps the
// address and gives the port back.
//#ExpectedStdout: ok
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef IP_BIND_ADDRESS_NO_PORT
#define IP_BIND_ADDRESS_NO_PORT 24
#endif

#define CHECK(c)                                                                     \
	do {                                                                         \
		if (!(c)) {                                                          \
			printf("FAIL %s:%d: %s (errno=%d %s)\n", __FILE__, __LINE__, \
			       #c, errno, strerror(errno));                          \
			exit(1);                                                     \
		}                                                                    \
	} while (0)

static socklen_t mk(int family, int port, struct sockaddr_storage *ss)
{
	memset(ss, 0, sizeof *ss);
	if (family == AF_INET) {
		struct sockaddr_in *a = (struct sockaddr_in *)ss;
		a->sin_family = AF_INET;
		a->sin_port = htons(port);
		a->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		return sizeof *a;
	}
	struct sockaddr_in6 *a = (struct sockaddr_in6 *)ss;
	a->sin6_family = AF_INET6;
	a->sin6_port = htons(port);
	a->sin6_addr = in6addr_loopback;
	return sizeof *a;
}

static int port_of(const struct sockaddr_storage *ss)
{
	return ntohs(ss->ss_family == AF_INET ? ((struct sockaddr_in *)ss)->sin_port
					      : ((struct sockaddr_in6 *)ss)->sin6_port);
}

static int is_loopback(const struct sockaddr_storage *ss)
{
	if (ss->ss_family == AF_INET)
		return ((struct sockaddr_in *)ss)->sin_addr.s_addr == htonl(INADDR_LOOPBACK);
	return IN6_IS_ADDR_LOOPBACK(&((struct sockaddr_in6 *)ss)->sin6_addr);
}

static struct sockaddr_storage name(int fd)
{
	struct sockaddr_storage ss;
	socklen_t len = sizeof ss;
	CHECK(getsockname(fd, (struct sockaddr *)&ss, &len) == 0);
	return ss;
}

// A socket bound to loopback without a port.
static int no_port(int family, int type)
{
	int fd = socket(family, type, 0);
	CHECK(fd >= 0);
	int one = 1, v = 0;
	socklen_t vl = sizeof v;
	CHECK(setsockopt(fd, SOL_IP, IP_BIND_ADDRESS_NO_PORT, &one, sizeof one) == 0);
	CHECK(getsockopt(fd, SOL_IP, IP_BIND_ADDRESS_NO_PORT, &v, &vl) == 0 && v == 1);
	struct sockaddr_storage ss;
	socklen_t len = mk(family, 0, &ss);
	CHECK(bind(fd, (struct sockaddr *)&ss, len) == 0);
	struct sockaddr_storage got = name(fd);
	CHECK(port_of(&got) == 0 && is_loopback(&got));
	// No port taken, so binding again is allowed.
	CHECK(bind(fd, (struct sockaddr *)&ss, len) == 0);
	return fd;
}

static int listener(int family, int *port)
{
	int fd = socket(family, SOCK_STREAM, 0);
	CHECK(fd >= 0);
	struct sockaddr_storage ss;
	socklen_t len = mk(family, 0, &ss);
	CHECK(bind(fd, (struct sockaddr *)&ss, len) == 0);
	CHECK(listen(fd, 4) == 0);
	struct sockaddr_storage got = name(fd);
	*port = port_of(&got);
	return fd;
}

static void tcp(int family)
{
	int lport;
	int l = listener(family, &lport);
	int c = no_port(family, SOCK_STREAM);
	struct sockaddr_storage ss;
	socklen_t len = mk(family, lport, &ss);
	CHECK(connect(c, (struct sockaddr *)&ss, len) == 0);
	struct sockaddr_storage me = name(c);
	CHECK(port_of(&me) != 0 && is_loopback(&me));
	struct sockaddr_storage peer;
	socklen_t plen = sizeof peer;
	int a = accept(l, (struct sockaddr *)&peer, &plen);
	CHECK(a >= 0);
	CHECK(port_of(&peer) == port_of(&me));
	close(a);
	close(c);

	// listen takes the port from the address.
	int l2 = no_port(family, SOCK_STREAM);
	CHECK(listen(l2, 4) == 0);
	me = name(l2);
	CHECK(port_of(&me) != 0 && is_loopback(&me));
	close(l2);
	close(l);
}

static void udp(int family)
{
	int r = socket(family, SOCK_DGRAM, 0);
	CHECK(r >= 0);
	struct sockaddr_storage ss;
	socklen_t len = mk(family, 0, &ss);
	CHECK(bind(r, (struct sockaddr *)&ss, len) == 0);
	struct sockaddr_storage rname = name(r);

	int u = no_port(family, SOCK_DGRAM);
	CHECK(sendto(u, "x", 1, 0, (struct sockaddr *)&rname, len) == 1);
	struct sockaddr_storage me = name(u), from;
	CHECK(port_of(&me) != 0 && is_loopback(&me));
	char buf[4];
	socklen_t flen = sizeof from;
	CHECK(recvfrom(r, buf, sizeof buf, 0, (struct sockaddr *)&from, &flen) == 1);
	CHECK(port_of(&from) == port_of(&me));

	// connect then disconnect: the port goes back, the address stays.
	CHECK(connect(u, (struct sockaddr *)&rname, len) == 0);
	struct sockaddr unspec = { .sa_family = AF_UNSPEC };
	CHECK(connect(u, &unspec, sizeof unspec) == 0);
	me = name(u);
	CHECK(port_of(&me) == 0 && is_loopback(&me));
	close(u);
	close(r);
}

int main(void)
{
	tcp(AF_INET);
	tcp(AF_INET6);
	udp(AF_INET);
	udp(AF_INET6);
	printf("ok\n");
	return 0;
}
