// RFC 3542 names the IPv6 hop-by-hop protocol hopopt (IPPROTO_HOPOPTS = 0).
// #ExpectedStdout: ok
#include <assert.h>
#include <netdb.h>
#include <stdio.h>
int main(void) {
  struct protoent *p = getprotobyname("hopopt");
  assert(p && p->p_proto == 0);
  p = getprotobyname("ip");
  assert(p && p->p_proto == 0);
  puts("ok");
}
