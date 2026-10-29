#include <assert.h>
#include <errno.h>
#include <libaio.h>
#include <time.h>
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
    assert(io_destroy(context) == 0);
    return 0;
}
