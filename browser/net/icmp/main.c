// Real host ICMP port unreachable, surfaced through a destination-qualified
// relay event. Linux net/ipv4/udp.c:__udp4_lib_err (now udp_err), udp_recvmsg;
// net/ipv4/ip_sockglue.c:ip_recv_error.
#define _GNU_SOURCE
// clang-format off
//#AbstractConfig: base
//#ExpectedStdout: ok
//#BrowserNetwork: fidelity
//#Config: connected4:base
//#Args: 4 connected
//#Config: connected6:base
//#Args: 6 connected
//#Config: recverr4:base
//#Args: 4 recverr
//#Config: recverr6:base
//#Args: 6 recverr
//#Config: unconnected4:base
//#Args: 4 unconnected
//#Config: unconnected6:base
//#Args: 6 unconnected
// clang-format on
// The Linux UAPI header needs libc's complete timespec first.
// clang-format off
#include <time.h>
#include <linux/errqueue.h>
// clang-format on
#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#define CHECK(c)                                             \
  do {                                                       \
    if (!(c)) {                                              \
      printf("FAIL %d: %s errno=%d\n", __LINE__, #c, errno); \
      exit(1);                                               \
    }                                                        \
  } while (0)
static struct sockaddr_storage address(int family, const char* ip, int port) {
  struct sockaddr_storage out = {0};
  if (family == AF_INET) {
    struct sockaddr_in* a = (void*)&out;
    a->sin_family = family;
    a->sin_port = htons(port);
    CHECK(inet_pton(family, ip, &a->sin_addr) == 1);
  } else {
    struct sockaddr_in6* a = (void*)&out;
    a->sin6_family = family;
    a->sin6_port = htons(port);
    CHECK(inet_pton(family, ip, &a->sin6_addr) == 1);
  }
  return out;
}
static void error_ready(int fd) {
  struct pollfd p = {fd, POLLIN, 0};
  CHECK(poll(&p, 1, 3000) == 1);
  CHECK(p.revents & POLLERR);
}
static int pending(int fd) {
  int e = -1;
  socklen_t len = sizeof e;
  CHECK(getsockopt(fd, SOL_SOCKET, SO_ERROR, &e, &len) == 0);
  return e;
}
int main(int argc, char** argv) {
  CHECK(argc == 3);
  int family = !strcmp(argv[1], "4") ? AF_INET : AF_INET6;
  int connected = !strcmp(argv[2], "connected"),
      recverr = !strcmp(argv[2], "recverr");
  const char* p = getenv(family == AF_INET ? "UDP_REFUSED" : "UDP6_REFUSED");
  CHECK(p && atoi(p));
  struct sockaddr_storage peer =
      address(family, family == AF_INET ? "192.0.2.1" : "2001:db8::1", atoi(p));
  socklen_t size = family == AF_INET ? sizeof(struct sockaddr_in)
                                     : sizeof(struct sockaddr_in6);
  int fd = socket(family, SOCK_DGRAM, 0);
  CHECK(fd >= 0);
  if (connected) CHECK(connect(fd, (void*)&peer, size) == 0);
  if (recverr) {
    int on = 1;
    CHECK(setsockopt(fd, family == AF_INET ? SOL_IP : SOL_IPV6,
                     family == AF_INET ? IP_RECVERR : IPV6_RECVERR, &on,
                     sizeof on) == 0);
  }
  CHECK(sendto(fd, "quote", 5, 0, (void*)&peer, size) == 5);
  if (connected) {
    error_ready(fd);
    char b[8];
    CHECK(recv(fd, b, sizeof b, 0) == -1 && errno == ECONNREFUSED);
    CHECK(pending(fd) == 0);
    CHECK(send(fd, "again", 5, 0) == 5);
    error_ready(fd);
    CHECK(send(fd, "again", 5, 0) == -1 && errno == ECONNREFUSED);
    CHECK(pending(fd) == 0);
  } else {
    // The control reply follows the relay's ICMP event on the same ordered
    // websocket, so absence of sk_err is checked only after the error was
    // actually observed.
    int control = socket(AF_INET, SOCK_STREAM, 0);
    CHECK(control >= 0);
    const char* port = getenv("TCP_CONTROL");
    CHECK(port);
    struct sockaddr_storage at = address(AF_INET, "192.0.2.1", atoi(port));
    CHECK(connect(control, (void*)&at, sizeof(struct sockaddr_in)) == 0);
    CHECK(write(control, "icmp-1", 6) == 6);
    char reply[2];
    size_t got = 0;
    while (got < 2) {
      ssize_t n = read(control, reply + got, 2 - got);
      CHECK(n > 0);
      got += n;
    }
    CHECK(!memcmp(reply, "ok", 2));
    close(control);
    CHECK(pending(fd) == (recverr ? ECONNREFUSED : 0));
    if (recverr) {
      error_ready(fd);
      char payload[16], ancillary[256];
      struct sockaddr_storage dst;
      struct iovec iov = {payload, sizeof payload};
      struct msghdr msg = {0};
      msg.msg_name = &dst;
      msg.msg_namelen = sizeof dst;
      msg.msg_iov = &iov;
      msg.msg_iovlen = 1;
      msg.msg_control = ancillary;
      msg.msg_controllen = sizeof ancillary;
      CHECK(recvmsg(fd, &msg, MSG_ERRQUEUE) == 5);
      CHECK(!memcmp(payload, "quote", 5));
      CHECK(msg.msg_flags & MSG_ERRQUEUE);
      struct cmsghdr* c = CMSG_FIRSTHDR(&msg);
      CHECK(c);
      CHECK(c->cmsg_level == (family == AF_INET ? SOL_IP : SOL_IPV6));
      CHECK(c->cmsg_type == (family == AF_INET ? IP_RECVERR : IPV6_RECVERR));
      struct sock_extended_err* e = (void*)CMSG_DATA(c);
      CHECK(e->ee_errno == ECONNREFUSED);
      CHECK(e->ee_origin ==
            (family == AF_INET ? SO_EE_ORIGIN_ICMP : SO_EE_ORIGIN_ICMP6));
      CHECK(e->ee_type == (family == AF_INET ? 3 : 1));
      CHECK(e->ee_code == (family == AF_INET ? 3 : 4));
      struct pollfd pollfd = {fd, POLLIN, 0};
      CHECK(poll(&pollfd, 1, 0) == 0);
    }
    char b;
    CHECK(recv(fd, &b, 1, MSG_DONTWAIT) == -1 && errno == EAGAIN);
  }
  close(fd);
  puts("ok");
  return 0;
}
