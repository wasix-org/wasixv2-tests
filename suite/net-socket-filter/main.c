// Classic BPF socket filters (SO_ATTACH_FILTER). Everyone: a UDP socket's filter sees the UDP
// header and payload (and the IP header at SKF_NET_OFF): 0 drops a datagram, a shorter length
// cuts its payload; SO_GET_FILTER counts instructions; a program that does not end in a return
// or a fprog of another size is EINVAL; SO_LOCK_FILTER keeps the filter (EPERM to detach,
// replace or unlock); a Unix datagram's receiver filters what is sent to it. With CAP_NET_RAW:
// a TCP socket takes a filter, and iputils ping's filter on a raw ICMP socket lets only echo
// replies of its identifier through.
//#ExpectedStdout: ok
#include <arpa/inet.h>
#include <errno.h>
#include <linux/filter.h>
#include <netinet/in.h>
#include <netinet/ip_icmp.h>
#include <poll.h>
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

#define LEN(a) (sizeof(a) / sizeof((a)[0]))

static int readable(int fd, int ms)
{
	struct pollfd p = { .fd = fd, .events = POLLIN };
	return poll(&p, 1, ms) == 1;
}

static int attach(int fd, struct sock_filter *insns, unsigned short n)
{
	struct sock_fprog prog = { .len = n, .filter = insns };
	return setsockopt(fd, SOL_SOCKET, SO_ATTACH_FILTER, &prog, sizeof prog);
}

static struct sockaddr_in lo4(unsigned short port)
{
	struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(port) };
	a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	return a;
}

static void udp(void)
{
	int rx = socket(AF_INET, SOCK_DGRAM, 0), tx = socket(AF_INET, SOCK_DGRAM, 0);
	CHECK(rx >= 0 && tx >= 0);
	struct sockaddr_in at = lo4(0);
	socklen_t len = sizeof at;
	CHECK(bind(rx, (struct sockaddr *)&at, sizeof at) == 0);
	CHECK(getsockname(rx, (struct sockaddr *)&at, &len) == 0);

	// The IP header says UDP, and the payload starts with 'k': keep the UDP header and 3
	// bytes of it; else drop.
	struct sock_filter keep_k[] = {
		BPF_STMT(BPF_LD | BPF_B | BPF_ABS, SKF_NET_OFF + 9),
		BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, IPPROTO_UDP, 0, 3),
		BPF_STMT(BPF_LD | BPF_B | BPF_ABS, 8),
		BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 'k', 0, 1),
		BPF_STMT(BPF_RET | BPF_K, 8 + 3),
		BPF_STMT(BPF_RET | BPF_K, 0),
	};
	CHECK(attach(rx, keep_k, LEN(keep_k)) == 0);
	CHECK(sendto(tx, "dropped", 7, 0, (struct sockaddr *)&at, sizeof at) == 7);
	CHECK(sendto(tx, "kept long", 9, 0, (struct sockaddr *)&at, sizeof at) == 9);
	char buf[64];
	CHECK(readable(rx, 2000));
	CHECK(recv(rx, buf, sizeof buf, 0) == 3 && memcmp(buf, "kep", 3) == 0);
	CHECK(!readable(rx, 200));

	// SO_GET_FILTER: the length counts instructions.
	struct sock_filter got[16];
	socklen_t n = 0;
	CHECK(getsockopt(rx, SOL_SOCKET, SO_GET_FILTER, got, &n) == 0 && n == LEN(keep_k));
	n = 2;
	CHECK(getsockopt(rx, SOL_SOCKET, SO_GET_FILTER, got, &n) == -1 && errno == EINVAL);
	n = LEN(got);
	CHECK(getsockopt(rx, SOL_SOCKET, SO_GET_FILTER, got, &n) == 0 && n == LEN(keep_k));
	CHECK(memcmp(got, keep_k, sizeof keep_k) == 0);
	int ext = 0;
	n = sizeof ext;
	CHECK(getsockopt(rx, SOL_SOCKET, SO_BPF_EXTENSIONS, &ext, &n) == 0 && ext == SKF_AD_MAX);

	// Invalid programs and values.
	struct sock_filter no_ret[] = { BPF_STMT(BPF_LD | BPF_IMM, 1) };
	CHECK(attach(rx, no_ret, 1) == -1 && errno == EINVAL);
	CHECK(attach(rx, keep_k, 0) == -1 && errno == EINVAL);
	struct sock_fprog prog = { .len = 1, .filter = keep_k };
	CHECK(setsockopt(rx, SOL_SOCKET, SO_ATTACH_FILTER, &prog, 4) == -1 && errno == EINVAL);

	// Detached, then locked.
	int zero = 0, one = 1;
	CHECK(setsockopt(rx, SOL_SOCKET, SO_DETACH_FILTER, &zero, sizeof zero) == 0);
	CHECK(setsockopt(rx, SOL_SOCKET, SO_DETACH_FILTER, &zero, sizeof zero) == -1 &&
	      errno == ENOENT);
	n = 0;
	CHECK(getsockopt(rx, SOL_SOCKET, SO_GET_FILTER, got, &n) == 0 && n == 0);
	CHECK(sendto(tx, "all", 3, 0, (struct sockaddr *)&at, sizeof at) == 3);
	CHECK(readable(rx, 2000) && recv(rx, buf, sizeof buf, 0) == 3);
	struct sock_filter drop_all[] = { BPF_STMT(BPF_RET | BPF_K, 0) };
	CHECK(attach(rx, drop_all, 1) == 0);
	CHECK(setsockopt(rx, SOL_SOCKET, SO_LOCK_FILTER, &one, sizeof one) == 0);
	CHECK(setsockopt(rx, SOL_SOCKET, SO_LOCK_FILTER, &zero, sizeof zero) == -1 &&
	      errno == EPERM);
	CHECK(setsockopt(rx, SOL_SOCKET, SO_DETACH_FILTER, &zero, sizeof zero) == -1 &&
	      errno == EPERM);
	CHECK(attach(rx, keep_k, LEN(keep_k)) == -1 && errno == EPERM);
	int locked = 0;
	n = sizeof locked;
	CHECK(getsockopt(rx, SOL_SOCKET, SO_LOCK_FILTER, &locked, &n) == 0 && locked == 1);
	CHECK(sendto(tx, "none", 4, 0, (struct sockaddr *)&at, sizeof at) == 4);
	CHECK(!readable(rx, 200));
	close(rx);
	close(tx);
}

static void unix_dgram(void)
{
	int sv[2];
	CHECK(socketpair(AF_UNIX, SOCK_DGRAM, 0, sv) == 0);
	struct sock_filter two[] = { BPF_STMT(BPF_RET | BPF_K, 2) };
	CHECK(attach(sv[1], two, 1) == 0);
	CHECK(send(sv[0], "hello", 5, 0) == 5);
	char buf[16];
	CHECK(recv(sv[1], buf, sizeof buf, MSG_DONTWAIT) == 2 && memcmp(buf, "he", 2) == 0);
	struct sock_filter none[] = { BPF_STMT(BPF_RET | BPF_K, 0) };
	CHECK(attach(sv[1], none, 1) == 0);
	CHECK(send(sv[0], "hello", 5, 0) == 5);
	CHECK(recv(sv[1], buf, sizeof buf, MSG_DONTWAIT) == -1 && errno == EAGAIN);
	close(sv[0]);
	close(sv[1]);
}

static unsigned short csum(const void *data, size_t len)
{
	const unsigned char *p = data;
	unsigned long sum = 0;
	for (size_t i = 0; i + 1 < len; i += 2)
		sum += (unsigned)(p[i] << 8 | p[i + 1]);
	while (sum >> 16)
		sum = (sum & 0xffff) + (sum >> 16);
	return (unsigned short)~sum;
}

static void send_echo(int r, unsigned short id)
{
	unsigned char req[16] = { ICMP_ECHO, 0, 0, 0, id >> 8, id & 0xff, 0, 1, 'w', 'a', 's', 'i' };
	unsigned short c = csum(req, sizeof req);
	req[2] = c >> 8;
	req[3] = c & 0xff;
	struct sockaddr_in to = lo4(0);
	CHECK(sendto(r, req, sizeof req, 0, (struct sockaddr *)&to, sizeof to) == sizeof req);
}

// iputils ping's ping4_install_filter: echo replies of our identifier, past the IP header.
static void raw_ping_filter(int r)
{
	unsigned short id = 0x4242;
	struct sock_filter insns[] = {
		BPF_STMT(BPF_LDX | BPF_B | BPF_MSH, 0),
		BPF_STMT(BPF_LD | BPF_H | BPF_IND, 4),
		BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, id, 0, 1),
		BPF_STMT(BPF_RET | BPF_K, ~0U),
		BPF_STMT(BPF_LD | BPF_B | BPF_IND, 0),
		BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, ICMP_ECHOREPLY, 1, 0),
		BPF_STMT(BPF_RET | BPF_K, 0xFFFFFFF),
		BPF_STMT(BPF_RET | BPF_K, 0),
	};
	// As iputils has it: anything of our identifier, other messages but echo replies.
	CHECK(attach(r, insns, LEN(insns)) == 0);
	send_echo(r, 0x1111);
	// The request to 0x1111 is not a reply: it passes; its reply is someone else's: dropped.
	CHECK(readable(r, 2000));
	unsigned char buf[256];
	ssize_t got = recv(r, buf, sizeof buf, 0);
	int ihl = (buf[0] & 0x0f) * 4;
	CHECK(got == ihl + 16 && buf[ihl] == ICMP_ECHO);
	CHECK(!readable(r, 200));
	send_echo(r, id);
	for (int i = 0; i < 2; i++) {
		CHECK(readable(r, 2000));
		got = recv(r, buf, sizeof buf, 0);
		CHECK(got == ihl + 16 && buf[ihl] == (i == 0 ? ICMP_ECHO : ICMP_ECHOREPLY));
	}
	CHECK(!readable(r, 200));
}

int main(void)
{
	udp();
	unix_dgram();
	int t = socket(AF_INET, SOCK_STREAM, 0);
	CHECK(t >= 0);
	struct sock_filter all[] = { BPF_STMT(BPF_RET | BPF_K, ~0U) };
	int r = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
	// Unprivileged, a TCP filter is EPERM since Linux 7.x (sk_is_tcp in sk_setsockopt), which
	// older hosts allow: only the privileged case is checked.
	if (r < 0) {
		CHECK(errno == EPERM);
	} else {
		CHECK(attach(t, all, 1) == 0);
		raw_ping_filter(r);
		close(r);
	}
	close(t);
	printf("ok\n");
	return 0;
}
