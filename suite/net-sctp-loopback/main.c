// SCTP over loopback with the raw socket API (no lksctp library): a one-to-one association
// with notifications, message boundaries, partial reads, send and receive info and a graceful
// shutdown; a one-to-many socket with an implicit association, association ids, connectx and
// peeloff through getsockopt, and bindx.
//#ExpectedStdout: ok
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <linux/sctp.h>

// lksctp's <netinet/sctp.h> defines it; the UAPI header leaves it to the libc.
#ifndef SOL_SCTP
#define SOL_SCTP IPPROTO_SCTP
#endif

#define CHECK(c)                                                                     \
	do {                                                                         \
		if (!(c)) {                                                          \
			printf("FAIL %s:%d: %s (errno=%d %s)\n", __FILE__, __LINE__, \
			       #c, errno, strerror(errno));                          \
			exit(1);                                                     \
		}                                                                    \
	} while (0)

static struct sockaddr_in loopback(unsigned short port)
{
	struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(port) };
	a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	return a;
}

static unsigned short port_of(int fd)
{
	struct sockaddr_in a;
	socklen_t len = sizeof a;
	CHECK(getsockname(fd, (struct sockaddr *)&a, &len) == 0);
	return ntohs(a.sin_port);
}

static void subscribe(int fd)
{
	struct sctp_event_subscribe ev;
	memset(&ev, 0, sizeof ev);
	ev.sctp_association_event = 1;
	ev.sctp_shutdown_event = 1;
	CHECK(setsockopt(fd, SOL_SCTP, SCTP_EVENTS, &ev, sizeof ev) == 0);
}

/// One recvmsg: its length, flags and (if `info`) the SCTP_RCVINFO.
static ssize_t receive(int fd, char *buf, size_t len, int *flags, struct sctp_rcvinfo *info)
{
	char control[CMSG_SPACE(sizeof(struct sctp_rcvinfo))];
	struct iovec iov = { .iov_base = buf, .iov_len = len };
	struct msghdr msg = {
		.msg_iov = &iov,
		.msg_iovlen = 1,
		.msg_control = control,
		.msg_controllen = sizeof control,
	};
	ssize_t n = recvmsg(fd, &msg, 0);
	if (n < 0)
		return n;
	*flags = msg.msg_flags;
	if (info) {
		struct cmsghdr *c = CMSG_FIRSTHDR(&msg);
		CHECK(c && c->cmsg_level == IPPROTO_SCTP && c->cmsg_type == SCTP_RCVINFO);
		memcpy(info, CMSG_DATA(c), sizeof *info);
	}
	return n;
}

/// The next notification, which must be `type`.
static void expect_notification(int fd, unsigned short type, unsigned short state)
{
	union sctp_notification n;
	int flags;
	ssize_t len = receive(fd, (char *)&n, sizeof n, &flags, NULL);
	CHECK(len >= (ssize_t)sizeof n.sn_header);
	CHECK((flags & MSG_NOTIFICATION) && (flags & MSG_EOR));
	CHECK(n.sn_header.sn_type == type);
	if (type == SCTP_ASSOC_CHANGE)
		CHECK(n.sn_assoc_change.sac_state == state);
}

static void one_to_one(void)
{
	int l = socket(AF_INET, SOCK_STREAM, IPPROTO_SCTP);
	CHECK(l >= 0);
	struct sockaddr_in a = loopback(0);
	CHECK(bind(l, (struct sockaddr *)&a, sizeof a) == 0);
	subscribe(l);
	CHECK(listen(l, 4) == 0);
	a = loopback(port_of(l));

	int c = socket(AF_INET, SOCK_STREAM, IPPROTO_SCTP);
	subscribe(c);
	CHECK(connect(c, (struct sockaddr *)&a, sizeof a) == 0);
	CHECK(connect(c, (struct sockaddr *)&a, sizeof a) == -1 && errno == EISCONN);
	int s = accept(l, NULL, NULL);
	CHECK(s >= 0);
	expect_notification(c, SCTP_ASSOC_CHANGE, SCTP_COMM_UP);
	expect_notification(s, SCTP_ASSOC_CHANGE, SCTP_COMM_UP);

	struct sctp_status status;
	memset(&status, 0, sizeof status);
	socklen_t len = sizeof status;
	CHECK(getsockopt(c, SOL_SCTP, SCTP_STATUS, &status, &len) == 0);
	CHECK(status.sstat_state == SCTP_ESTABLISHED && status.sstat_outstrms == 10);

	// Boundaries survive, and a short buffer reads a message in parts.
	CHECK(send(c, "first", 5, 0) == 5);
	CHECK(send(c, "second!", 7, 0) == 7);
	char buf[64];
	int flags;
	CHECK(receive(s, buf, sizeof buf, &flags, NULL) == 5 && (flags & MSG_EOR));
	CHECK(receive(s, buf, 4, &flags, NULL) == 4 && !(flags & MSG_EOR));
	CHECK(receive(s, buf, sizeof buf, &flags, NULL) == 3 && (flags & MSG_EOR));
	CHECK(memcmp(buf, "nd!", 3) == 0);

	// SCTP_SNDINFO in, SCTP_RCVINFO out.
	int on = 1;
	CHECK(setsockopt(s, SOL_SCTP, SCTP_RECVRCVINFO, &on, sizeof on) == 0);
	char control[CMSG_SPACE(sizeof(struct sctp_sndinfo))];
	memset(control, 0, sizeof control);
	struct iovec iov = { .iov_base = "info", .iov_len = 4 };
	struct msghdr msg = {
		.msg_iov = &iov,
		.msg_iovlen = 1,
		.msg_control = control,
		.msg_controllen = sizeof control,
	};
	struct cmsghdr *cm = CMSG_FIRSTHDR(&msg);
	cm->cmsg_level = IPPROTO_SCTP;
	cm->cmsg_type = SCTP_SNDINFO;
	cm->cmsg_len = CMSG_LEN(sizeof(struct sctp_sndinfo));
	struct sctp_sndinfo snd = { .snd_sid = 3, .snd_ppid = htonl(42) };
	memcpy(CMSG_DATA(cm), &snd, sizeof snd);
	CHECK(sendmsg(c, &msg, 0) == 4);
	struct sctp_rcvinfo rcv;
	CHECK(receive(s, buf, sizeof buf, &flags, &rcv) == 4);
	CHECK(rcv.rcv_sid == 3 && rcv.rcv_ppid == htonl(42));

	// A stream past the association's.
	snd.snd_sid = 10;
	memcpy(CMSG_DATA(cm), &snd, sizeof snd);
	CHECK(sendmsg(c, &msg, 0) == -1 && errno == EINVAL);

	// A graceful shutdown: the peer reads what came first, the shutdown, its end, then EOF.
	CHECK(send(c, "last", 4, 0) == 4);
	CHECK(shutdown(c, SHUT_WR) == 0);
	CHECK(receive(s, buf, sizeof buf, &flags, NULL) == 4);
	expect_notification(s, SCTP_SHUTDOWN_EVENT, 0);
	expect_notification(s, SCTP_ASSOC_CHANGE, SCTP_SHUTDOWN_COMP);
	CHECK(recv(s, buf, sizeof buf, 0) == 0);
	expect_notification(c, SCTP_ASSOC_CHANGE, SCTP_SHUTDOWN_COMP);
	close(s);
	close(c);
	close(l);
}

static void one_to_many(void)
{
	int srv = socket(AF_INET, SOCK_SEQPACKET, 0);
	CHECK(srv >= 0);
	int type;
	socklen_t len = sizeof type;
	CHECK(getsockopt(srv, SOL_SOCKET, SO_PROTOCOL, &type, &len) == 0 && type == IPPROTO_SCTP);
	// bindx: two addresses of one port.
	struct sockaddr_in two[2] = { loopback(0), loopback(0) };
	CHECK(setsockopt(srv, SOL_SCTP, SCTP_SOCKOPT_BINDX_ADD, &two[0], sizeof two[0]) == 0);
	two[0].sin_port = two[1].sin_port = htons(port_of(srv));
	two[1].sin_addr.s_addr = htonl(0x7f000002);
	CHECK(setsockopt(srv, SOL_SCTP, SCTP_SOCKOPT_BINDX_ADD, &two[1], sizeof two[1]) == 0);
	CHECK(listen(srv, 4) == 0);

	// An implicit association by sendto.
	int c1 = socket(AF_INET, SOCK_SEQPACKET, IPPROTO_SCTP);
	CHECK(sendto(c1, "hello", 5, 0, (struct sockaddr *)&two[0], sizeof two[0]) == 5);
	// One without an address or association is EPIPE.
	CHECK(send(c1, "x", 1, MSG_NOSIGNAL) == -1 && errno == EPIPE);

	// connectx returns the association id.
	int c2 = socket(AF_INET, SOCK_SEQPACKET, IPPROTO_SCTP);
	int id2 = setsockopt(c2, SOL_SCTP, SCTP_SOCKOPT_CONNECTX, two, sizeof two);
	CHECK(id2 > 0);
	CHECK(sendto(c2, "world", 5, 0, (struct sockaddr *)&two[1], sizeof two[1]) == 5);

	// The server's associations, and the first message with its id.
	char buf[64];
	struct sockaddr_in from;
	socklen_t flen = sizeof from;
	CHECK(recvfrom(srv, buf, sizeof buf, 0, (struct sockaddr *)&from, &flen) == 5);
	CHECK(memcmp(buf, "hello", 5) == 0 && from.sin_port == htons(port_of(c1)));
	uint32_t count;
	len = sizeof count;
	CHECK(getsockopt(srv, SOL_SCTP, SCTP_GET_ASSOC_NUMBER, &count, &len) == 0 && count == 2);
	// `struct sctp_assoc_ids`: the count, then the ids.
	sctp_assoc_t list[3];
	len = sizeof list;
	CHECK(getsockopt(srv, SOL_SCTP, SCTP_GET_ASSOC_ID_LIST, list, &len) == 0);
	CHECK(list[0] == 2);

	// Peel the association c2 made off the server: its message goes with it.
	sctp_peeloff_arg_t peel = { .associd = list[2] };
	len = sizeof peel;
	int fd = getsockopt(srv, SOL_SCTP, SCTP_SOCKOPT_PEELOFF, &peel, &len);
	CHECK(fd >= 0 && peel.sd == fd);
	CHECK(recv(peel.sd, buf, sizeof buf, 0) == 5 && memcmp(buf, "world", 5) == 0);
	CHECK(getsockopt(srv, SOL_SCTP, SCTP_GET_ASSOC_NUMBER, &count, &len) == 0 && count == 1);

	// A reply on the peeled-off socket needs no address.
	CHECK(send(peel.sd, "back", 4, 0) == 4);
	CHECK(recv(c2, buf, sizeof buf, 0) == 4 && memcmp(buf, "back", 4) == 0);
	close(peel.sd);
	close(c1);
	close(c2);
	close(srv);
}

int main(void)
{
	one_to_one();
	one_to_many();
	printf("ok\n");
	return 0;
}
