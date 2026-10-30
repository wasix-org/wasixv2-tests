// In-runtime peer networking; no relay or host socket is used.
// Linux net/ipv4/tcp.c:tcp_shutdown,tcp_recvmsg;
// net/ipv4/udp.c:udp_recvmsg,udp_err.
#define _GNU_SOURCE
// clang-format off
//#AbstractConfig: base
//#ExpectedStdout: ok
//#Config: tcp4:base
//#Args: 4 tcp
//#BrowserNetwork: switch4
//#Config: udp4:base
//#Args: 4 udp
//#BrowserNetwork: switch4
//#Config: tcp6:base
//#Args: 6 tcp
//#BrowserNetwork: switch6
//#Config: udp6:base
//#Args: 6 udp
//#BrowserNetwork: switch6
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
static void check_source(const struct sockaddr_storage* source, int family) {
  const void* ip;
  if (family == AF_INET) {
    const struct sockaddr_in* a = (const void*)source;
    CHECK(ntohs(a->sin_port) == 8080);
    ip = &a->sin_addr;
  } else {
    const struct sockaddr_in6* a = (const void*)source;
    CHECK(ntohs(a->sin6_port) == 8080);
    ip = &a->sin6_addr;
  }
  char text[INET6_ADDRSTRLEN];
  CHECK(inet_ntop(family, ip, text, sizeof text));
  CHECK(!strcmp(text, family == AF_INET ? "192.0.2.10" : "fd00::10"));
}
static void exact(int fd, const char* text) {
  char buf[16];
  size_t n = strlen(text), got = 0;
  while (got < n) {
    ssize_t r = read(fd, buf + got, n - got);
    CHECK(r > 0);
    got += r;
  }
  CHECK(!memcmp(buf, text, n));
}
int main(int argc, char** argv) {
  CHECK(argc == 3 || argc == 4);
  int family = !strcmp(argv[1], "4") ? AF_INET : AF_INET6;
  int udp = !strcmp(argv[2], "udp"), server = argc == 4;
  socklen_t size = family == AF_INET ? sizeof(struct sockaddr_in)
                                     : sizeof(struct sockaddr_in6);
  const char* any = family == AF_INET ? "0.0.0.0" : "::";
  const char* lo = family == AF_INET ? "127.0.0.1" : "::1";
  struct sockaddr_storage local = address(family, any, 8080);
  struct sockaddr_storage peer =
      address(family, family == AF_INET ? "192.0.2.11" : "fd00::11", 8080);
  int fd = socket(family, udp ? SOCK_DGRAM : SOCK_STREAM, 0);
  CHECK(fd >= 0);
  // Both Machines can own the SAME guest port, with separate namespace bind
  // tables.
  CHECK(bind(fd, (void*)&local, size) == 0);
  if (server) {
    if (udp) {
      char b[8];
      struct sockaddr_storage from;
      socklen_t len = sizeof from;
      CHECK(recvfrom(fd, b, sizeof b, 0, (void*)&from, &len) == 4);
      CHECK(!memcmp(b, "ping", 4));
      check_source(&from, family);
      CHECK(sendto(fd, "pong", 4, 0, (void*)&from, len) == 4);
    } else {
      // Loopback-only listener remains private to this Machine.
      int private = socket(family, SOCK_STREAM, 0);
      CHECK(private >= 0);
      struct sockaddr_storage p = address(family, lo, 9000);
      CHECK(bind(private, (void*)&p, size) == 0);
      CHECK(listen(private, 1) == 0);
      CHECK(listen(fd, 1) == 0);
      struct sockaddr_storage from;
      socklen_t len = sizeof from;
      int client = accept(fd, (void*)&from, &len);
      CHECK(client >= 0);
      check_source(&from, family);
      exact(client, "ping");
      CHECK(write(client, "pong", 4) == 4);
      CHECK(shutdown(client, SHUT_WR) == 0);
      exact(client, "after-fin");
      close(client);
      close(private);
    }
  } else {
    CHECK(connect(fd, (void*)&peer, size) == 0);
    CHECK(write(fd, "ping", 4) == 4);
    exact(fd, "pong");
    if (udp) {
      peer =
          address(family, family == AF_INET ? "192.0.2.11" : "fd00::11", 9001);
      CHECK(connect(fd, (void*)&peer, size) == 0);
      CHECK(write(fd, "probe", 5) == 5);
      struct pollfd p = {fd, POLLIN, 0};
      CHECK(poll(&p, 1, 3000) == 1);
      CHECK(p.revents & POLLERR);
      char b[8];
      CHECK(recv(fd, b, sizeof b, 0) == -1 && errno == ECONNREFUSED);
      CHECK(recv(fd, b, sizeof b, MSG_DONTWAIT) == -1 && errno == EAGAIN);
    } else {
      char b;
      CHECK(read(fd, &b, 1) == 0);
      int private = socket(family, SOCK_STREAM, 0);
      CHECK(private >= 0);
      struct sockaddr_storage p = address(family, lo, 9000);
      CHECK(connect(private, (void*)&p, size) == -1 && errno == ECONNREFUSED);
      close(private);
      CHECK(write(fd, "after-fin", 9) == 9);
    }
    puts("ok");
  }
  close(fd);
  return 0;
}
