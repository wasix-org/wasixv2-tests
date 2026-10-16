// A thread blocked in recv on a UDP socket while another thread sends from it to a host
// address: what comes back from the host must wake the sleeper, which went to sleep before the
// socket had anything outside the namespace. Inside the namespace, a datagram from another
// socket wakes it the same way.
//#ExpectedStdout: ok
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define CHECK(c)                                                                     \
	do {                                                                         \
		if (!(c)) {                                                          \
			printf("FAIL %s:%d: %s (errno=%d %s)\n", __FILE__, __LINE__, \
			       #c, errno, strerror(errno));                          \
			exit(1);                                                     \
		}                                                                    \
	} while (0)

struct wait {
	int fd;
	ssize_t got;
	int err;
};

static void *receiver(void *arg)
{
	struct wait *w = arg;
	char buf[16];
	w->got = recv(w->fd, buf, sizeof buf, 0);
	w->err = errno;
	return NULL;
}

int main(void)
{
	// Five seconds at most, instead of sleeping forever if the wakeup is lost.
	struct timeval tv = { .tv_sec = 5 };

	int fd = socket(AF_INET, SOCK_DGRAM, 0);
	CHECK(fd >= 0);
	CHECK(setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv) == 0);
	struct wait w = { .fd = fd };
	pthread_t t;
	CHECK(pthread_create(&t, NULL, receiver, &w) == 0);
	usleep(200 * 1000);
	struct sockaddr_in host = { .sin_family = AF_INET, .sin_port = htons(1) };
	CHECK(inet_pton(AF_INET, "10.0.2.2", &host.sin_addr) == 1);
	CHECK(connect(fd, (struct sockaddr *)&host, sizeof host) == 0);
	CHECK(send(fd, "query", 5, 0) == 5);
	CHECK(pthread_join(t, NULL) == 0);
	errno = w.err;
	CHECK(w.got == -1 && w.err == ECONNREFUSED);

	// In the namespace.
	int a = socket(AF_INET, SOCK_DGRAM, 0);
	int b = socket(AF_INET, SOCK_DGRAM, 0);
	CHECK(a >= 0 && b >= 0);
	CHECK(setsockopt(a, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv) == 0);
	struct sockaddr_in lo = { .sin_family = AF_INET };
	lo.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	CHECK(bind(a, (struct sockaddr *)&lo, sizeof lo) == 0);
	socklen_t len = sizeof lo;
	CHECK(getsockname(a, (struct sockaddr *)&lo, &len) == 0);
	w = (struct wait){ .fd = a };
	CHECK(pthread_create(&t, NULL, receiver, &w) == 0);
	usleep(200 * 1000);
	CHECK(sendto(b, "hi", 2, 0, (struct sockaddr *)&lo, sizeof lo) == 2);
	CHECK(pthread_join(t, NULL) == 0);
	CHECK(w.got == 2);
	puts("ok");
	return 0;
}
