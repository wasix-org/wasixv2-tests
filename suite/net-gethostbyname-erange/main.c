// glibc nss/digits_dots.c __nss_hostname_digits_dots_context checks the
// numeric result's storage before parsing it; a small buffer is ERANGE.
// #ExpectedStdout: ok
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

int main(void) {
  struct hostent h, *result;
  struct {
    char buf[1024];
    char canary[16];
  } storage;
  memset(&storage, 'X', sizeof storage);
  char name[1000];
  memset(name, '0', sizeof name - 1);
  name[sizeof name - 1] = 0;
  int error = 0;
  result = &h;
  assert(gethostbyname_r(name, &h, storage.buf, sizeof storage.buf, &result,
                         &error) == ERANGE);
  assert(result == NULL && error == -1 /* NETDB_INTERNAL */);
  for (unsigned i = 0; i < sizeof storage; ++i)
    assert(((char *)&storage)[i] == 'X');
  char large[4096];
  assert(gethostbyname_r(name, &h, large, sizeof large, &result, &error) == 0);
  assert(result == &h && strcmp(h.h_name, name) == 0);
  assert(memcmp(h.h_addr_list[0], "\0\0\0\0", 4) == 0);
  const char *names[] = {"127.0.0.1", "::1", "localhost"};
  for (unsigned i = 0; i < 3; ++i) {
    int family = i == 1 ? AF_INET6 : AF_INET;
    result = &h;
    error = 0;
    assert(gethostbyname2_r(names[i], family, &h, storage.buf + 1, 1, &result,
                            &error) == ERANGE);
    assert(result == NULL && error == -1 /* NETDB_INTERNAL */);
    assert(gethostbyname2_r(names[i], family, &h, storage.buf + 1,
                            sizeof storage.buf - 1, &result, &error) == 0);
    assert(result == &h && h.h_addr_list[0]);
    assert(h.h_addrtype == family &&
           h.h_length == (family == AF_INET6 ? 16 : 4));
  }
  puts("ok");
}
