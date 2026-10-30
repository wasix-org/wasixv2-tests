// Linux include/asm-generic/shmparam.h: SHMLBA is PAGE_SIZE;
// ipc/shm.c:do_shmat and mm/mremap.c use that alignment.
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <unistd.h>

int main(void)
{
    assert(SHMLBA == getpagesize());
    size_t page = SHMLBA;
    int fd = open("/tmp/shmlba", O_CREAT | O_TRUNC | O_RDWR, 0600);
    assert(fd >= 0);
    assert(ftruncate(fd, 4 * page) == 0);
    unsigned char *p = mmap(NULL, 4 * page, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    assert(p != MAP_FAILED);
    for (size_t i = 0; i < 4; i++)
        p[i * page] = 10 + i;
    assert(msync(p, 4 * page, MS_SYNC) == 0);
    assert(mremap(p + page, page, page, MREMAP_MAYMOVE | MREMAP_FIXED, p + 3 * page) == p + 3 * page);
    assert(p[3 * page] == 11);
    assert(mremap(p + 3 * page, page, page, MREMAP_MAYMOVE | MREMAP_FIXED, p + page) == p + page);
    assert(p[page] == 11);
    assert(remap_file_pages(p, page, 0, 2, 0) == 0);
    assert(p[0] == 12);
    assert(munmap(p, 4 * page) == 0);
    assert(close(fd) == 0);
    assert(unlink("/tmp/shmlba") == 0);
    puts("PASS");
}
