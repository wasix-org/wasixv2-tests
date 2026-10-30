// net/ipv4/udp.c udp_sendmsg/udp_lib_setsockopt and net/ipv6/udp.c
// udpv6_sendmsg: pending sends form one datagram using the first destination.
//#ExpectedStdout: ok
#include <assert.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/udp.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static char large[65527];
static void check(int family)
{
    struct sockaddr_storage first = {0}, second;
    socklen_t size;
    if (family == AF_INET) {
        struct sockaddr_in *a = (void *)&first;
        a->sin_family = family;
        a->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        size = sizeof *a;
    } else {
        struct sockaddr_in6 *a = (void *)&first;
        a->sin6_family = family;
        a->sin6_addr = in6addr_loopback;
        size = sizeof *a;
    }
    int sender = socket(family, SOCK_DGRAM, 0);
    int receiver = socket(family, SOCK_DGRAM, 0);
    int other = socket(family, SOCK_DGRAM, 0);
    assert(sender >= 0 && receiver >= 0 && other >= 0);
    assert(bind(receiver, (void *)&first, size) == 0);
    assert(getsockname(receiver, (void *)&first, &size) == 0);
    second = first;
    if (family == AF_INET) ((struct sockaddr_in *)&second)->sin_port = 0;
    else ((struct sockaddr_in6 *)&second)->sin6_port = 0;
    assert(bind(other, (void *)&second, size) == 0);
    assert(getsockname(other, (void *)&second, &size) == 0);
    char buf[32];
    assert(sendto(sender, "hello", 5, MSG_MORE, (void *)&first, size) == 5);
    assert(recv(receiver, buf, sizeof buf, MSG_DONTWAIT) == -1 && errno == EAGAIN);
    assert(sendto(sender, " world", 6, 0, (void *)&second, size) == 6);
    assert(recv(receiver, buf, sizeof buf, MSG_DONTWAIT) == 11 && memcmp(buf, "hello world", 11) == 0);
    assert(recv(other, buf, sizeof buf, MSG_DONTWAIT) == -1 && errno == EAGAIN);
    int one = 1, zero = 0;
    assert(setsockopt(sender, IPPROTO_UDP, UDP_CORK, &one, sizeof one) == 0);
    assert(sendto(sender, "a", 1, 0, (void *)&first, size) == 1);
    assert(send(sender, "b", 1, 0) == 1);
    assert(recv(receiver, buf, sizeof buf, MSG_DONTWAIT) == -1 && errno == EAGAIN);
    assert(setsockopt(sender, IPPROTO_UDP, UDP_CORK, &zero, sizeof zero) == 0);
    assert(recv(receiver, buf, sizeof buf, MSG_DONTWAIT) == 2 && memcmp(buf, "ab", 2) == 0);
    assert(sendto(sender, "lost", 4, MSG_MORE, (void *)&first, size) == 4);
    assert(send(sender, large, sizeof large, 0) == -1 && errno == EMSGSIZE);
    assert(send(sender, "x", 1, 0) == -1 && errno == EDESTADDRREQ);
    assert(recv(receiver, buf, sizeof buf, MSG_DONTWAIT) == -1 && errno == EAGAIN);
    if (family == AF_INET6) {
        struct sockaddr_in6 mapped = { .sin6_family = AF_INET6, .sin6_port = htons(11111) };
        mapped.sin6_addr.s6_addr[10] = mapped.sin6_addr.s6_addr[11] = 0xff;
        mapped.sin6_addr.s6_addr[12] = 127;
        mapped.sin6_addr.s6_addr[15] = 1;
        assert(sendto(sender, "a", 1, MSG_MORE, (void *)&first, size) == 1);
        assert(sendto(sender, "wrong", 5, 0, (void *)&mapped, sizeof mapped) == -1 && errno == EINVAL);
        assert(send(sender, "b", 1, 0) == 1);
        assert(recv(receiver, buf, sizeof buf, MSG_DONTWAIT) == 2 && memcmp(buf, "ab", 2) == 0);
    }
    assert(sendto(sender, "discard", 7, MSG_MORE, (void *)&first, size) == 7);
    close(sender);
    assert(recv(receiver, buf, sizeof buf, MSG_DONTWAIT) == -1 && errno == EAGAIN);
    close(receiver);
    close(other);
}
int main(void) { check(AF_INET); check(AF_INET6); puts("ok"); }
