// The default root's /etc/services supplies service names to musl getaddrinfo.
// #ExpectedStdout: ok
#include <assert.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
int main(void) {
  struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM};
  struct addrinfo *res;
  assert(getaddrinfo("127.0.0.1", "echo", &hints, &res) == 0);
  assert(ntohs(((struct sockaddr_in *)res->ai_addr)->sin_port) == 7);
  freeaddrinfo(res);
  hints.ai_socktype = SOCK_DGRAM;
  assert(getaddrinfo("127.0.0.1", "domain", &hints, &res) == 0);
  assert(ntohs(((struct sockaddr_in *)res->ai_addr)->sin_port) == 53);
  freeaddrinfo(res);
  puts("ok");
}
