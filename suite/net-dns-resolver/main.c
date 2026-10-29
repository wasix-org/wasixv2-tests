// Name resolution through the name server /etc/resolv.conf names, by default the machine's own
// DNS server (10.0.2.3). getaddrinfo asks it over UDP for a name under "localhost", which no
// hosts file has and every resolver answers with the loopback addresses (RFC 6761 6.3); then
// a query by hand over TCP (RFC 7766), after which the server closes once the client has.
//
// With arguments NAME COUNT, getaddrinfo must also find COUNT IPv4 addresses of NAME: with
// enough of them the UDP answer is truncated (TC) and libc asks again over TCP. That needs a
// name the resolver's host knows that well, so only runs against a prepared resolver (a hosts
// file or stub server in a private network and mount namespace) do it.
//#ExpectedStdout: udp dns-test.localhost 127.0.0.1 ::1
//#ExpectedStdout: tcp dns-test.localhost 127.0.0.1
//#ExpectedStdout: ok
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
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

static const char *NAME = "dns-test.localhost";

/* The first IPv4 name server of /etc/resolv.conf. */
static struct in_addr nameserver(void)
{
	FILE *f = fopen("/etc/resolv.conf", "r");
	CHECK(f != NULL);
	char line[256], addr[64];
	struct in_addr ns;
	while (fgets(line, sizeof line, f)) {
		if (sscanf(line, "nameserver %63s", addr) == 1 && inet_pton(AF_INET, addr, &ns) == 1) {
			fclose(f);
			return ns;
		}
	}
	printf("FAIL no IPv4 nameserver in /etc/resolv.conf\n");
	exit(1);
}

static void udp(void)
{
	struct addrinfo hints = { .ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM };
	struct addrinfo *res = NULL;
	int rc = getaddrinfo(NAME, NULL, &hints, &res);
	if (rc != 0) {
		printf("FAIL getaddrinfo %s: %s\n", NAME, gai_strerror(rc));
		exit(1);
	}
	int v4 = 0, v6 = 0;
	for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
		if (ai->ai_family == AF_INET) {
			struct sockaddr_in *sin = (struct sockaddr_in *)ai->ai_addr;
			v4 += sin->sin_addr.s_addr == htonl(INADDR_LOOPBACK);
		} else if (ai->ai_family == AF_INET6) {
			struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)ai->ai_addr;
			v6 += IN6_IS_ADDR_LOOPBACK(&sin6->sin6_addr);
		}
	}
	freeaddrinfo(res);
	CHECK(v4 == 1 && v6 == 1);
	printf("udp %s 127.0.0.1 ::1\n", NAME);
}

/* A query for `name` of type A, with recursion desired; returns its length. */
static size_t query(unsigned char *q, unsigned id, const char *name)
{
	unsigned char hdr[12] = { id >> 8, id & 0xff, 1, 0, 0, 1 };
	memcpy(q, hdr, sizeof hdr);
	size_t n = sizeof hdr;
	for (const char *l = name; *l;) {
		const char *dot = strchr(l, '.');
		size_t len = dot ? (size_t)(dot - l) : strlen(l);
		q[n++] = len;
		memcpy(q + n, l, len);
		n += len;
		l += len + (dot != NULL);
	}
	q[n++] = 0;
	unsigned char tail[4] = { 0, 1, 0, 1 };
	memcpy(q + n, tail, 4);
	return n + 4;
}

static void read_all(int fd, unsigned char *buf, size_t len)
{
	for (size_t got = 0; got < len;) {
		ssize_t r = read(fd, buf + got, len - got);
		CHECK(r > 0);
		got += r;
	}
}

static void tcp(void)
{
	struct sockaddr_in sa = { .sin_family = AF_INET, .sin_port = htons(53),
				  .sin_addr = nameserver() };
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	CHECK(fd >= 0);
	CHECK(connect(fd, (struct sockaddr *)&sa, sizeof sa) == 0);
	unsigned char msg[2 + 512];
	size_t len = query(msg + 2, 0x4242, NAME);
	msg[0] = len >> 8;
	msg[1] = len & 0xff;
	CHECK(write(fd, msg, 2 + len) == (ssize_t)(2 + len));
	unsigned char lenbuf[2], ans[65535];
	read_all(fd, lenbuf, 2);
	size_t alen = lenbuf[0] << 8 | lenbuf[1];
	CHECK(alen >= 12 + len - 12 + 16);
	read_all(fd, ans, alen);
	CHECK(ans[0] == 0x42 && ans[1] == 0x42);
	CHECK((ans[2] & 0x80) && (ans[3] & 0x0f) == 0);
	CHECK((ans[6] << 8 | ans[7]) >= 1);
	CHECK(memcmp(ans + alen - 4, "\x7f\0\0\x01", 4) == 0);
	// Once the client closes its side, so does the server.
	CHECK(shutdown(fd, SHUT_WR) == 0);
	CHECK(read(fd, ans, 1) == 0);
	close(fd);
	printf("tcp %s 127.0.0.1\n", NAME);
}

static void many(const char *name, int count)
{
	struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_STREAM };
	struct addrinfo *res = NULL;
	int rc = getaddrinfo(name, NULL, &hints, &res);
	if (rc != 0) {
		printf("FAIL getaddrinfo %s: %s\n", name, gai_strerror(rc));
		exit(1);
	}
	int n = 0;
	for (struct addrinfo *ai = res; ai; ai = ai->ai_next)
		n++;
	freeaddrinfo(res);
	if (n != count) {
		printf("FAIL %s: %d addresses, want %d\n", name, n, count);
		exit(1);
	}
	printf("many %s %d\n", name, n);
}

int main(int argc, char **argv)
{
	udp();
	tcp();
	if (argc == 3)
		many(argv[1], atoi(argv[2]));
	printf("ok\n");
	return 0;
}
