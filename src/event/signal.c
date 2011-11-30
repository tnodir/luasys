/* Signals */

#include <sys/wait.h>


/* Global signal events */
static struct {
    pthread_mutex_t cs;
    struct event *events[NSIG];
} g_Signal;
static int volatile g_SignalInit = 0;


static void
signal_handler (int signo)
{
#ifdef USE_KQUEUE
    (void) signo;
#else
    struct event *ev;

    if (signo == SYS_SIGINTR) return;

    pthread_mutex_lock(&g_Signal.cs);
    ev = g_Signal.events[signo];
    for (; ev; ev = ev->next_object) {
	if (!event_deleted(ev))
	    evq_signal(ev->evq, signo);
    }
    pthread_mutex_unlock(&g_Signal.cs);
#endif
}

void
signal_init (void)
{
    if (g_SignalInit) return;
    g_SignalInit = 1;

    /* Initialize critical section */
    {
	pthread_mutexattr_t mattr;

	pthread_mutexattr_init(&mattr);
	pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&g_Signal.cs, &mattr);
	pthread_mutexattr_destroy(&mattr);
    }
    /* To interrupt blocking syscalls */
    signal_set(SYS_SIGINTR, signal_handler);
}

int
evq_signal (struct event_queue *evq, int signo)
{
    int res = 0;

    pthread_mutex_lock(&evq->cs);
    if (!evq->sig_ready)
	res = evq_interrupt(evq);
    evq->sig_ready |= 1 << signo;
    pthread_mutex_unlock(&evq->cs);
    return res;
}

int
evq_interrupt (struct event_queue *evq)
{
#ifdef USE_EVENTFD
    const fd_t fd = evq->sig_fd[0];
    const int64_t data = 1;
#else
    const fd_t fd = evq->sig_fd[1];
    const char data = 0;
#endif
    int nw;

    do nw = write(fd, &data, sizeof(data));
    while (nw == -1 && errno == EINTR);

    return (nw == -1) ? -1 : 0;
}

int
signal_set (int signo, sig_handler_t func)
{
    struct sigaction act;
    int res;

    act.sa_handler = func;
    sigemptyset(&act.sa_mask);
    act.sa_flags = SA_RESTART;

    do res = sigaction(signo, &act, NULL);
    while (res == -1 && errno == EINTR);

    return res;
}

#ifdef USE_KQUEUE
static int
signal_kqueue (struct event_queue *evq, int signo, int action)
{
    struct kevent kev;
    int res;

    kev.ident = signo;
    kev.filter = EVFILT_SIGNAL;
    kev.flags = action;
    kev.udata = NULL;

    do res = kevent(evq->kqueue_fd, &kev, 1, NULL, 0, NULL);
    while (res == -1 && errno == EINTR);

    return res;
}
#endif

int
evq_ignore_signal (struct event_queue *evq, int signo, int ignore)
{
#ifndef USE_KQUEUE
    (void) evq;
#else
    if (signal_kqueue(evq, signo, ignore ? EV_DELETE : EV_ADD))
	return -1;
#endif
    if (ignore)
	return signal_set(signo, SIG_IGN);
    else {
	int res;

	pthread_mutex_lock(&g_Signal.cs);
	res = signal_set(signo,
	 (g_Signal.events[signo] ? signal_handler : SIG_DFL));
	pthread_mutex_unlock(&g_Signal.cs);
	return res;
    }
}

static int
signal_add (struct event_queue *evq, struct event *ev)
{
    const int signo = (ev->flags & EVENT_PID) ? SIGCHLD : (int) ev->fd;
    struct event **sig_evp;

    pthread_mutex_lock(&g_Signal.cs);
    sig_evp = &g_Signal.events[signo];
    if (*sig_evp)
	ev->next_object = *sig_evp;
    else {
#ifdef USE_KQUEUE
	if (signal_kqueue(evq, signo, EV_ADD))
	    goto err;
#endif
	if (signal_set(signo, signal_handler))
	    goto err;
	ev->next_object = NULL;
    }
    *sig_evp = ev;
    pthread_mutex_unlock(&g_Signal.cs);

    evq->nevents++;
    return 0;
 err:
    pthread_mutex_unlock(&g_Signal.cs);
    return -1;
}

static int
signal_del (struct event_queue *evq, struct event *ev)
{
    const int signo = (ev->flags & EVENT_PID) ? SIGCHLD : (int) ev->fd;
    struct event **sig_evp;
    int res = 0;

    pthread_mutex_lock(&g_Signal.cs);
    sig_evp = &g_Signal.events[signo];
    if (*sig_evp == ev) {
	if (!(*sig_evp = ev->next_object)) {
#ifndef USE_KQUEUE
	    (void) evq;
#else
	    res |= signal_kqueue(evq, signo, EV_DELETE);
#endif
	    res |= signal_set(signo, SIG_DFL);
	}
    } else {
	struct event *sig_ev = *sig_evp;

	while (sig_ev->next_object != ev)
	    sig_ev = sig_ev->next_object;
	sig_ev->next_object = ev->next_object;
    }
    pthread_mutex_unlock(&g_Signal.cs);
    return res;
}

static struct event *
signal_process_active (struct event *ev, struct event *ev_ready, msec_t now)
{
    ev->flags |= EVENT_ACTIVE | EVENT_READ_RES;
    if (ev->flags & EVENT_ONESHOT)
	evq_del(ev, 1);
    else if (ev->tq && !(ev->flags & EVENT_TIMEOUT_MANUAL))
	timeout_reset(ev, now);

    ev->next_ready = ev_ready;
    return ev;
}

static int
signal_process_child (struct event *ev)
{
    int fd = (int) ev->fd;
    int pid, status;

    do pid = waitpid(fd, &status, WNOHANG);
    while (pid == -1 && errno == EINTR);

    if (pid == fd) {
	ev->flags |= !WIFEXITED(status) ? EVENT_EOF_MASK_RES
	 : ((unsigned int) WEXITSTATUS(status) << EVENT_EOF_SHIFT_RES);
	return 0;
    }
    return -1;
}

static struct event *
signal_process_actives (struct event_queue *evq, int signo, struct event *ev_ready, msec_t now)
{
    struct event *ev;

    pthread_mutex_lock(&g_Signal.cs);
    ev = g_Signal.events[signo];
    for (; ev; ev = ev->next_object) {
	if (ev->evq == evq) {
	    if (signo == SIGCHLD && signal_process_child(ev))
		continue;
	    ev_ready = signal_process_active(ev, ev_ready, now);
	}
    }
    pthread_mutex_unlock(&g_Signal.cs);
    return ev_ready;
}

static struct event *
signal_process_interrupt (struct event_queue *evq, struct event *ev_ready, msec_t now)
{
    unsigned int sig_ready;
    int signo;

    pthread_mutex_lock(&evq->cs);
    /* reset interruption event */
    {
	const fd_t fd = evq->sig_fd[0];
#ifdef USE_EVENTFD
	char buf[8];
#else
	char buf[BUFSIZ];
#endif
	int nr;

	do nr = read(fd, buf, sizeof(buf));
	while ((nr == -1 && errno == EINTR)
#ifndef USE_EVENTFD
	 || nr == sizeof(buf)
#endif
	);
    }
    sig_ready = evq->sig_ready;
    evq->sig_ready = 0;
    pthread_mutex_unlock(&evq->cs);

    for (signo = 0; sig_ready; ++signo, sig_ready >>= 1) {
	if (sig_ready & 1)
	    ev_ready = signal_process_actives(evq, signo, ev_ready, now);
    }
    return ev_ready;
}

