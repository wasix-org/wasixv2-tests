#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <libaio.h>
#include <time.h>
#include <sys/uio.h>
#include <unistd.h>
/* Linux include/uapi/linux/aio_abi.h: fixed-width request and completion records. */
_Static_assert(sizeof(struct iocb) == 64, "iocb ABI");
_Static_assert(sizeof(struct io_event) == 32, "io_event ABI");

int main(void)
{
    io_context_t context = 0;
    errno = EDOM;
    assert(io_setup(8, &context) == 0);
    assert(context != 0);
    assert(io_submit(context, 0, 0) == 0);
    struct iocb request = {0};
    assert(io_cancel(context, &request, 0) == -EINVAL);
    assert(errno == EDOM);
    struct timespec timeout = {0, 1};
    struct io_event event;
    assert(io_getevents(context, 0, 1, &event, &timeout) == 0);
    assert(io_pgetevents(context, 0, 1, &event, &timeout, 0) == 0);
    assert(io_pgetevents(0, 0, 1, &event, &timeout, 0) == -EINVAL);
    int descriptors[2];
    assert(pipe(descriptors) == 0);
    char byte;
    struct iovec vector = { &byte, 1 };
    struct iocb *requests[] = { &request };
    for (int vectored = 0; vectored < 2; vectored++) {
        if (vectored)
            io_prep_preadv(&request, descriptors[0], &vector, 1, 0);
        else
            io_prep_pread(&request, descriptors[0], &byte, 1, 0);
        request.aio_rw_flags = RWF_NOWAIT;
        assert(io_submit(context, 1, requests) == 1);
        assert(io_getevents(context, 1, 1, &event, &timeout) == 1);
        assert(event.res == -EAGAIN);
    }
    assert(close(descriptors[0]) == 0);
    assert(close(descriptors[1]) == 0);
    assert(io_destroy(context) == 0);
    return 0;
}
