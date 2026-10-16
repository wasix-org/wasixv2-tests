// mq_notify: SIGEV_SIGNAL delivers the signal with si_code SI_MESGQ and the registered value
// when a message reaches an empty queue, once; a second registration is EBUSY; a waiting
// receiver takes the message instead of the notification; SIGEV_NONE registers without a
// signal; mq_notify(NULL) unregisters; SIGEV_THREAD runs the function on a new thread.
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: CHECK(%s) failed (errno %s)\n", __LINE__, #c, strerror(errno)); fails++; } } while (0)
#define FAILS(call, e) do { errno = 0; long r_ = (long)(call); if (r_ != -1 || errno != (e)) { fprintf(stderr, "line %d: %s = %ld (%s), want -1 %s\n", __LINE__, #call, r_, strerror(errno), #e); fails++; } } while (0)

static volatile sig_atomic_t got_signal, got_code, got_value;

static void handler(int sig, siginfo_t *info, void *ctx)
{
	(void)ctx;
	got_signal = sig;
	got_code = info->si_code;
	got_value = info->si_value.sival_int;
}

static sem_t thread_ran;
static volatile int thread_value;

static void on_message(union sigval value)
{
	thread_value = value.sival_int;
	sem_post(&thread_ran);
}

static void *receiver(void *arg)
{
	mqd_t q = *(mqd_t *)arg;
	char buf[16];
	CHECK(mq_receive(q, buf, sizeof buf, NULL) == 2);
	return NULL;
}

int main(void)
{
	struct sigaction sa = {0};
	sa.sa_sigaction = handler;
	sa.sa_flags = SA_SIGINFO;
	sigaction(SIGUSR1, &sa, NULL);

	struct mq_attr attr = {.mq_maxmsg = 4, .mq_msgsize = 16};
	mq_unlink("/notify");
	mqd_t q = mq_open("/notify", O_CREAT | O_RDWR, 0600, &attr);
	CHECK(q != (mqd_t)-1);
	char buf[16];

	struct sigevent sev = {0};
	sev.sigev_notify = SIGEV_SIGNAL;
	sev.sigev_signo = SIGUSR1;
	sev.sigev_value.sival_int = 42;
	CHECK(mq_notify(q, &sev) == 0);
	FAILS(mq_notify(q, &sev), EBUSY);
	CHECK(mq_send(q, "m1", 2, 0) == 0);
	for (int i = 0; i < 1000 && !got_signal; i++)
		usleep(1000);
	CHECK(got_signal == SIGUSR1 && got_code == SI_MESGQ && got_value == 42);

	/* Fired once: the registration is gone, and a send to a non-empty queue notifies nobody. */
	got_signal = 0;
	CHECK(mq_send(q, "m2", 2, 0) == 0);
	CHECK(mq_receive(q, buf, sizeof buf, NULL) == 2);
	CHECK(mq_receive(q, buf, sizeof buf, NULL) == 2);
	CHECK(mq_send(q, "m3", 2, 0) == 0);
	usleep(10000);
	CHECK(got_signal == 0);
	CHECK(mq_receive(q, buf, sizeof buf, NULL) == 2);

	/* A receiver already waiting takes the message; the notification stays registered. */
	CHECK(mq_notify(q, &sev) == 0);
	pthread_t t;
	CHECK(pthread_create(&t, NULL, receiver, &q) == 0);
	usleep(20000);
	CHECK(mq_send(q, "m4", 2, 0) == 0);
	CHECK(pthread_join(t, NULL) == 0);
	usleep(10000);
	CHECK(got_signal == 0);
	FAILS(mq_notify(q, &sev), EBUSY);
	CHECK(mq_notify(q, NULL) == 0);
	CHECK(mq_notify(q, &sev) == 0);
	CHECK(mq_notify(q, NULL) == 0);

	/* SIGEV_NONE takes the registration without a signal. */
	struct sigevent none = {.sigev_notify = SIGEV_NONE};
	CHECK(mq_notify(q, &none) == 0);
	CHECK(mq_send(q, "m5", 2, 0) == 0);
	usleep(10000);
	CHECK(got_signal == 0);
	CHECK(mq_receive(q, buf, sizeof buf, NULL) == 2);
	CHECK(mq_notify(q, &sev) == 0);
	CHECK(mq_notify(q, NULL) == 0);

	/* SIGEV_THREAD: libc's helper thread hears of the message over a netlink socket. */
	sem_init(&thread_ran, 0, 0);
	struct sigevent thread = {0};
	thread.sigev_notify = SIGEV_THREAD;
	thread.sigev_notify_function = on_message;
	thread.sigev_value.sival_int = 7;
	CHECK(mq_notify(q, &thread) == 0);
	CHECK(mq_send(q, "m6", 2, 0) == 0);
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_sec += 5;
	CHECK(sem_timedwait(&thread_ran, &ts) == 0 && thread_value == 7);

	struct sigevent bad = {.sigev_notify = 99};
	FAILS(mq_notify(q, &bad), EINVAL);
	FAILS(mq_notify(0, &none), EBADF);
	CHECK(mq_close(q) == 0);
	CHECK(mq_unlink("/notify") == 0);

	printf(fails ? "FAIL\n" : "PASS\n");
	return fails != 0;
}
