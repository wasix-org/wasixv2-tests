// Browser relay acceptance. Linux
// net/ipv4/tcp.c:tcp_recvmsg_locked,tcp_shutdown, tcp_poll,tcp_recv_urg;
// net/ipv4/udp.c:udp_recvmsg,udp_sendmsg.
#define _GNU_SOURCE
// clang-format off
//#AbstractConfig: base
//#ExpectedStdout: ok
//#BrowserNetwork: fidelity
//#Config: peer:base
//#Args: peer-client
//#BrowserNetwork: peer
//#Config: tcp4:base
//#Args: tcp4
//#BrowserNetwork: classic
//#Config: tcp6:base
//#Args: tcp6
//#BrowserNetwork: classic
//#Config: udp4:base
//#Args: udp4
//#BrowserNetwork: classic
//#Config: udp6:base
//#Args: udp6
//#BrowserNetwork: classic
//#Config: dns:base
//#Args: dns
//#Config: half-close:base
//#Args: half-close
//#Config: reset:base
//#Args: reset
//#Config: udp-error:base
//#Args: udp-error
//#Config: urgent:base
//#Args: urgent
//#Config: queues:base
//#Args: queues
//#Config: absent:base
//#Args: absent
//#BrowserNetwork: absent
//#Config: refused:base
//#Args: refused
//#Config: ws-refused:base
//#Args: ws-refused
//#BrowserNetwork: refused
//#Config: disconnect:base
//#Args: disconnect
//#BrowserNetwork: disconnect
//#Config: overflow:base
//#Args: overflow
//#BrowserNetwork: overflow
//#Config: ephemeral:base
//#Args: ingress
//#BrowserNetwork: ephemeral
//#Config: ingress:base
//#Args: ingress
//#BrowserNetwork: ingress
// clang-format on
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#define CHECK(c)                                                  \
  do {                                                            \
    if (!(c)) {                                                   \
      printf("FAIL line %d: %s errno=%d\n", __LINE__, #c, errno); \
      exit(1);                                                    \
    }                                                             \
  } while (0)
static struct sockaddr_storage addr(int family, const char* host, int port) {
  struct sockaddr_storage out = {0};
  if (family == AF_INET) {
    struct sockaddr_in* a = (void*)&out;
    a->sin_family = family;
    a->sin_port = htons(port);
    CHECK(inet_pton(family, host, &a->sin_addr) == 1);
  } else {
    struct sockaddr_in6* a = (void*)&out;
    a->sin6_family = family;
    a->sin6_port = htons(port);
    CHECK(inet_pton(family, host, &a->sin6_addr) == 1);
  }
  return out;
}
static socklen_t size(int family) {
  return family == AF_INET ? sizeof(struct sockaddr_in)
                           : sizeof(struct sockaddr_in6);
}
static int port(const char* name) {
  const char* v = getenv(name);
  CHECK(v);
  int n = atoi(v);
  CHECK(n > 0 && n < 65536);
  return n;
}
static int open_socket(int family, int kind, int p) {
  int fd = socket(family, kind, 0);
  CHECK(fd >= 0);
  struct sockaddr_storage a =
      addr(family, family == AF_INET ? "192.0.2.1" : "2001:db8::1", p);
  CHECK(connect(fd, (void*)&a, size(family)) == 0);
  return fd;
}
static void read_exact(int fd, char* out, size_t n) {
  size_t at = 0;
  while (at < n) {
    ssize_t got = read(fd, out + at, n - at);
    CHECK(got > 0);
    at += got;
  }
}
static void tcp(int family) {
  int fd = open_socket(family, SOCK_STREAM,
                       port(family == AF_INET ? "TCP_ECHO" : "TCP6_ECHO"));
  char b[8] = {0};
  CHECK(write(fd, "ping", 4) == 4);
  read_exact(fd, b, 4);
  CHECK(!memcmp(b, "ping", 4));
  CHECK(shutdown(fd, SHUT_WR) == 0);
  CHECK(read(fd, b, 8) == 0);
  CHECK(send(fd, "x", 1, MSG_NOSIGNAL) == -1 && errno == EPIPE);
  close(fd);
}
static void udp(int family) {
  int fd = open_socket(family, SOCK_DGRAM,
                       port(family == AF_INET ? "UDP_ECHO" : "UDP6_ECHO"));
  char b[8];
  CHECK(send(fd, "", 0, 0) == 0);
  struct pollfd p = {fd, POLLIN, 0};
  CHECK(poll(&p, 1, 5000) == 1 && (p.revents & POLLIN));
  CHECK(recv(fd, b, 8, MSG_PEEK) == 0);
  CHECK(recv(fd, b, 8, 0) == 0);
  CHECK(send(fd, "abcdef", 6, 0) == 6);
  CHECK(recv(fd, b, 3, MSG_PEEK | MSG_TRUNC) == 6);
  CHECK(!memcmp(b, "abc", 3));
  CHECK(recv(fd, b, 3, MSG_TRUNC) == 6);
  close(fd);
}
static void dns(void) {
  struct addrinfo hints = {.ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM},
                  *list = NULL;
  CHECK(getaddrinfo("relay.test", NULL, &hints, &list) == 0);
  int v4 = 0, v6 = 0;
  for (struct addrinfo* a = list; a; a = a->ai_next) {
    char host[NI_MAXHOST];
    CHECK(getnameinfo(a->ai_addr, a->ai_addrlen, host, sizeof host, NULL, 0,
                      NI_NAMEREQD) == 0);
    CHECK(!strcmp(host, "relay.test"));
    v4 |= a->ai_family == AF_INET;
    v6 |= a->ai_family == AF_INET6;
  }
  freeaddrinfo(list);
  CHECK(v4 && v6);
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  CHECK(fd >= 0);
  struct sockaddr_storage server = addr(AF_INET, "10.0.2.3", 53);
  const unsigned char query[] = {
      0x12, 0x34, 1,   0,   0, 1,   0,   0,   0,   0, 0, 0,  5, 'r',
      'e',  'l',  'a', 'y', 4, 't', 'e', 's', 't', 0, 0, 16, 0, 1};
  CHECK(sendto(fd, query, sizeof query, 0, (void*)&server,
               sizeof(struct sockaddr_in)) == sizeof query);
  unsigned char response[512];
  ssize_t n = recv(fd, response, sizeof response, 0);
  CHECK(n > (ssize_t)sizeof query);
  CHECK(response[0] == 0x12 && response[1] == 0x34 && response[7] == 1);
  CHECK(!memcmp(response + n - 5, "relay", 5));
  close(fd);
}
static void half_close(void) {
  int fd = open_socket(AF_INET, SOCK_STREAM, port("TCP_FIN"));
  char b[8];
  read_exact(fd, b, 3);
  CHECK(!memcmp(b, "fin", 3));
  CHECK(read(fd, b, 8) == 0);
  CHECK(write(fd, "after-fin", 9) == 9);
  CHECK(shutdown(fd, SHUT_WR) == 0);
  int control = open_socket(AF_INET, SOCK_STREAM, port("TCP_CONTROL"));
  CHECK(write(control, "fin-wait", 8) == 8);
  read_exact(control, b, 2);
  CHECK(!memcmp(b, "ok", 2));
  close(control);
  close(fd);
}
static void reset(void) {
  int fd = open_socket(AF_INET, SOCK_STREAM, port("TCP_RESET"));
  CHECK(write(fd, "reset", 5) == 5);
  char b;
  CHECK(read(fd, &b, 1) == -1 && errno == ECONNRESET);
  CHECK(read(fd, &b, 1) == 0);
  CHECK(send(fd, "x", 1, MSG_NOSIGNAL) == -1 && errno == EPIPE);
  close(fd);
}
static void udp_error(void) {
  int fd = open_socket(AF_INET, SOCK_DGRAM, port("UDP_ECHO"));
  struct sockaddr_storage bad = addr(AF_INET, "198.51.100.1", 9);
  CHECK(sendto(fd, "x", 1, 0, (void*)&bad, sizeof(struct sockaddr_in)) == 1);
  struct pollfd p = {fd, POLLERR, 0};
  CHECK(poll(&p, 1, 5000) == 1 && (p.revents & POLLERR));
  int error = 0;
  socklen_t n = sizeof error;
  CHECK(getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &n) == 0);
  CHECK(error == EACCES);
  CHECK(send(fd, "ok", 2, 0) == 2);
  char b[2];
  CHECK(recv(fd, b, 2, 0) == 2 && !memcmp(b, "ok", 2));
  close(fd);
}
static void urgent(void) {
  int fd = open_socket(AF_INET, SOCK_STREAM, 9);
  CHECK(send(fd, "ab!", 3, MSG_OOB) == 3);
  CHECK(write(fd, "cd", 2) == 2);
  struct pollfd p = {fd, POLLPRI, 0};
  CHECK(poll(&p, 1, 5000) == 1 && (p.revents & POLLPRI));
  char b[8];
  read_exact(fd, b, 2);
  CHECK(!memcmp(b, "ab", 2));
  int mark = 0;
  CHECK(ioctl(fd, SIOCATMARK, &mark) == 0 && mark);
  CHECK(recv(fd, b, 1, MSG_OOB | MSG_PEEK) == 1 && b[0] == '!');
  CHECK(recv(fd, b, 1, MSG_OOB) == 1 && b[0] == '!');
  CHECK(recv(fd, b, 1, MSG_OOB) == -1 && errno == EINVAL);
  read_exact(fd, b, 2);
  CHECK(!memcmp(b, "cd", 2));
  close(fd);
}
static void queues(void) {
  int fd = open_socket(AF_INET, SOCK_STREAM, port("TCP_SINK"));
  CHECK(fcntl(fd, F_SETFL, O_NONBLOCK) == 0);
  static char data[65536];
  size_t total = 0;
  for (;;) {
    ssize_t n = send(fd, data, sizeof data, MSG_NOSIGNAL);
    if (n < 0) {
      CHECK(errno == EAGAIN);
      break;
    }
    CHECK(n > 0);
    total += n;
    CHECK(total <= 4 * 1024 * 1024);
  }
  CHECK(total > 0);
  close(fd);
}
static void failure(const char* mode) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  CHECK(fd >= 0);
  struct sockaddr_storage a = addr(
      AF_INET, "192.0.2.1", !strcmp(mode, "refused") ? port("TCP_REFUSED") : 9);
  int rc = connect(fd, (void*)&a, sizeof(struct sockaddr_in));
  if (!strcmp(mode, "absent")) {
    CHECK(rc == -1 && errno == ENETUNREACH);
  } else if (!strcmp(mode, "refused")) {
    CHECK(rc == -1 && errno == ECONNREFUSED);
  } else if (!strcmp(mode, "ws-refused")) {
    // Infrastructure failed before creation (ENETDOWN) or aborted
    // an existing flow.
    CHECK(rc == -1 && (errno == ENETDOWN || errno == ECONNABORTED));
  } else {
    char b[4096];
    if (rc == 0) {
      do {
        rc = read(fd, b, sizeof b);
      } while (rc > 0);
    }
    CHECK(rc == -1 && errno == ECONNABORTED);
  }
  close(fd);
}
static void ingress(void) {
  int srv = socket(AF_INET, SOCK_STREAM, 0);
  CHECK(srv >= 0);
  struct sockaddr_storage a = addr(AF_INET, "0.0.0.0", 8080);
  CHECK(bind(srv, (void*)&a, sizeof(struct sockaddr_in)) == 0);
  CHECK(listen(srv, 1) == 0);
  int control = open_socket(AF_INET, SOCK_STREAM, port("TCP_CONTROL"));
  CHECK(write(control, "tcp", 3) == 3);
  int fd = accept(srv, NULL, NULL);
  CHECK(fd >= 0);
  char b[16];
  read_exact(fd, b, 7);
  CHECK(!memcmp(b, "ingress", 7));
  CHECK(write(fd, "tcp-reply", 9) == 9);
  CHECK(shutdown(fd, SHUT_WR) == 0);
  close(fd);
  read_exact(control, b, 2);
  CHECK(!memcmp(b, "ok", 2));
  close(control);
  close(srv);
  fd = socket(AF_INET, SOCK_DGRAM, 0);
  CHECK(fd >= 0);
  a = addr(AF_INET, "0.0.0.0", 8081);
  CHECK(bind(fd, (void*)&a, sizeof(struct sockaddr_in)) == 0);
  control = open_socket(AF_INET, SOCK_STREAM, port("TCP_CONTROL"));
  CHECK(write(control, "udp", 3) == 3);
  struct sockaddr_storage from;
  socklen_t len = sizeof from;
  CHECK(recvfrom(fd, b, sizeof b, 0, (void*)&from, &len) == 11);
  CHECK(!memcmp(b, "udp-ingress", 11));
  CHECK(sendto(fd, "udp-reply", 9, 0, (void*)&from, len) == 9);
  read_exact(control, b, 2);
  close(control);
  close(fd);
}
static void peer(int server) {
  char b[16];
  int srv = -1;
  if (server) {
    srv = socket(AF_INET, SOCK_STREAM, 0);
    CHECK(srv >= 0);
    struct sockaddr_storage a = addr(AF_INET, "0.0.0.0", 8080);
    CHECK(bind(srv, (void*)&a, sizeof(struct sockaddr_in)) == 0);
    CHECK(listen(srv, 1) == 0);
  }
  int control = open_socket(AF_INET, SOCK_STREAM, port("TCP_CONTROL"));
  const char* message = server ? "peer-ready" : "peer-wait";
  CHECK(write(control, message, strlen(message)) == (ssize_t)strlen(message));
  read_exact(control, b, 2);
  CHECK(!memcmp(b, "ok", 2));
  close(control);
  int fd = server ? accept(srv, NULL, NULL)
                  : open_socket(AF_INET, SOCK_STREAM, port("PUBLISH_TCP"));
  CHECK(fd >= 0);
  if (server) {
    read_exact(fd, b, 6);
    CHECK(!memcmp(b, "client", 6));
    CHECK(write(fd, "peer", 4) == 4);
    close(srv);
  } else {
    CHECK(write(fd, "client", 6) == 6);
    read_exact(fd, b, 4);
    CHECK(!memcmp(b, "peer", 4));
  }
  close(fd);
}
int main(int argc, char** argv) {
  CHECK(argc == 2);
  const char* m = argv[1];
  if (!strcmp(m, "peer-client"))
    peer(0);
  else if (!strcmp(m, "peer-server"))
    peer(1);
  else if (!strcmp(m, "tcp4"))
    tcp(AF_INET);
  else if (!strcmp(m, "tcp6"))
    tcp(AF_INET6);
  else if (!strcmp(m, "udp4"))
    udp(AF_INET);
  else if (!strcmp(m, "udp6"))
    udp(AF_INET6);
  else if (!strcmp(m, "dns"))
    dns();
  else if (!strcmp(m, "half-close"))
    half_close();
  else if (!strcmp(m, "reset"))
    reset();
  else if (!strcmp(m, "udp-error"))
    udp_error();
  else if (!strcmp(m, "urgent"))
    urgent();
  else if (!strcmp(m, "queues"))
    queues();
  else if (!strcmp(m, "ingress"))
    ingress();
  else
    failure(m);
  puts("ok");
  return 0;
}
