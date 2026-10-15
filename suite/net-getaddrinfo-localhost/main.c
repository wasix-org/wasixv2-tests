// getaddrinfo without DNS: numeric hosts, the passive wildcard, and "localhost", which libc
// resolves from /etc/hosts. getnameinfo with NI_NUMERICHOST.
//#ExpectedStdout: numeric 127.0.0.1:80
//#ExpectedStdout: numeric6 ::1:443
//#ExpectedStdout: passive 0.0.0.0:8080
//#ExpectedStdout: localhost 127.0.0.1:22
//#ExpectedStdout: ok
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
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

static void show(const char *label, const char *host, const char *serv, int family, int flags)
{
	struct addrinfo hints = { .ai_family = family, .ai_socktype = SOCK_STREAM,
				  .ai_flags = flags };
	struct addrinfo *res = NULL;
	int rc = getaddrinfo(host, serv, &hints, &res);
	if (rc != 0) {
		printf("FAIL %s: getaddrinfo: %s\n", label, gai_strerror(rc));
		exit(1);
	}
	CHECK(res != NULL && res->ai_socktype == SOCK_STREAM);
	char h[NI_MAXHOST], s[NI_MAXSERV];
	CHECK(getnameinfo(res->ai_addr, res->ai_addrlen, h, sizeof h, s, sizeof s,
			  NI_NUMERICHOST | NI_NUMERICSERV) == 0);
	printf("%s %s:%s\n", label, h, s);
	freeaddrinfo(res);
}

int main(void)
{
	show("numeric", "127.0.0.1", "80", AF_UNSPEC, AI_NUMERICHOST);
	show("numeric6", "::1", "443", AF_INET6, AI_NUMERICHOST);
	show("passive", NULL, "8080", AF_INET, AI_PASSIVE);
	show("localhost", "localhost", "22", AF_INET, 0);

	struct addrinfo hints = { .ai_flags = AI_NUMERICHOST };
	struct addrinfo *res;
	CHECK(getaddrinfo("not-a-number", NULL, &hints, &res) == EAI_NONAME);
	printf("ok\n");
	return 0;
}
