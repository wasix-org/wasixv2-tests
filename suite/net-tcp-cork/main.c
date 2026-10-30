// net/ipv4/tcp.c tcp_push, tcp_output.c tcp_write_xmit, and include/net/tcp.h
// tcp_check_probe_timer: MSG_MORE withholds a partial segment until push or
// probe.
// #ExpectedStdout: ok
#include <assert.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(void) {
  int listener = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in address = {.sin_family = AF_INET,
                                .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
  assert(listener >= 0 &&
         bind(listener, (void *)&address, sizeof address) == 0);
  assert(listen(listener, 1) == 0);
  socklen_t size = sizeof address;
  assert(getsockname(listener, (void *)&address, &size) == 0);
  int sender = socket(AF_INET, SOCK_STREAM, 0);
  assert(sender >= 0 && connect(sender, (void *)&address, size) == 0);
  int receiver = accept(listener, NULL, NULL);
  assert(receiver >= 0);
  char buf[16];
  assert(send(sender, "held", 4, MSG_MORE) == 4);
  assert(recv(receiver, buf, sizeof buf, MSG_DONTWAIT) == -1 &&
         errno == EAGAIN);
  struct pollfd pfd = {.fd = receiver, .events = POLLIN};
  assert(poll(&pfd, 1, 50) == 0);
  assert(poll(&pfd, 1, 1000) == 1 && (pfd.revents & POLLIN));
  assert(recv(receiver, buf, sizeof buf, 0) == 4 &&
         memcmp(buf, "held", 4) == 0);
  int one = 1, zero = 0;
  assert(setsockopt(sender, IPPROTO_TCP, TCP_CORK, &one, sizeof one) == 0);
  assert(send(sender, "a", 1, 0) == 1);
  assert(recv(receiver, buf, sizeof buf, MSG_DONTWAIT) == -1 &&
         errno == EAGAIN);
  assert(setsockopt(sender, IPPROTO_TCP, TCP_CORK, &zero, sizeof zero) == 0);
  assert(recv(receiver, buf, sizeof buf, 0) == 1 && buf[0] == 'a');
  close(sender);
  close(receiver);
  close(listener);
  puts("ok");
}
