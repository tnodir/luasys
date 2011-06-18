/* Signals */

#include <sys/wait.h>


/* Global signal events */
static struct event *g_SigEvents[NSIG];


static void
signal_handler (int signo)
{
#ifdef USE_KQUEUE
    (void) signo;
#else
    const struct event *ev = g_SigEvents[signo];

    if (ev) {
	const fd_t fd = ev->evq->sig_fd[1];

	while (write(fd, &signo, 1) == -1 && errno == EINTR)
	    continue;
    }
#endif
}

int
evq_interrupt (struct event_queue *evq)
{
    const int signo = SYS_SIGINTR;

#ifdef USE_KQUEUE
    (void) evq;
    return kill(getpid(), signo);
#else
    const fd_t fd = evq->sig_fd[1];
    int nw;

    do nw = write(fd, &signo, 1);
    while (nw == -1 && errno == EINTR);

    return (nw == -1) ? -1 : 0;
#endif
}

int
signal_set (int signo, sig_handler_t func)
{
    struct sigaction act;
    int res;

    act.sa_handler = (signo != SYS_SIGINTR) ? func : signal_handler;
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
    return signal_set(signo, ignore ? SIG_IGN
     : (g_SigEvents[signo] ? signal_handler : SIG_DFL));
}

static int
signal_add (struct event_queue *evq, struct event *ev)
{
    const int signo = (ev->flags & EVENT_PID) ? SIGCHLD : (int) ev->fd;
    struct event **sig_evp = &g_SigEvents[signo];

    if (*sig_evp)
	ev->next_object = *sig_evp;
    else {
#ifdef USE_KQUEUE
	if (signal_kqueue(evq, signo, EV_ADD))
	    return -1;
#endif
	if (signal_set(signo, signal_handler))
	    return -1;
	ev->next_object = NULL;
    }
    *sig_evp = ev;
    evq->nevents++;
    return 0;
}

static int
signal_del (struct event_queue *evq, struct event *ev)
{
    const int signo = (ev->flags & EVENT_PID) ? SIGCHLD : (int) ev->fd;
    struct event **sig_evp = &g_SigEvents[signo];

    if (*sig_evp == ev) {
	if (!(*sig_evp = ev->next_object)) {
	    int res = 0;

#ifndef USE_KQUEUE
	    (void) evq;
#else
	    res |= signal_kqueue(evq, signo, EV_DELETE);
#endif
	    res |= signal_set(signo, SIG_DFL);
	    return res;
	}
    } else {
	struct event *sig_ev = *sig_evp;

	while (sig_ev->next_object != ev)
	    sig_ev = sig_ev->next_object;
	sig_ev->next_object = ev->next_object;
    }
    return 0;
}

static struct event *
signal_active (struct event *ev, struct event *ev_ready, msec_t now)
{
    ev->flags |= EVENT_ACTIVE | EVENT_READ_RES;
    if (ev->flags & EVENT_ONESHOT)
	evq_del(ev, 1);
    else if (ev->tq)
	timeout_reset(ev, now);

    ev->next_ready = ev_ready;
    return ev;
}

static struct event *
signal_actives (int signo, struct event *ev_ready, msec_t now)
{
    struct event *ev = g_SigEvents[signo];

    for (; ev; ev = ev->next_object)
	ev_ready = signal_active(ev, ev_ready, now);

    return ev_ready;
}

static struct event *
signal_children (struct event *ev_ready, msec_t now)
{
    for (; ; ) {
	struct event *ev;
	int pid, status;

	do pid = waitpid(-1, &status, WNOHANG);
	while (pid == -1 && errno == EINTR);
	if (pid == -1)
	    return ev_ready;

	for (ev = g_SigEvents[SIGCHLD]; ev; ev = ev->next_object)
	    if ((int) ev->fd == pid) {
		ev->flags |= !WIFEXITED(status) ? EVENT_EOF_MASK_RES
		 : ((unsigned int) WEXITSTATUS(status) << EVENT_EOF_SHIFT_RES);
		ev_ready = signal_active(ev, ev_ready, now);
		break;
	    }
    }
}

#ifndef USE_KQUEUE
static struct event *
signal_process (struct event_queue *evq, struct event *ev_ready, msec_t now)
{
    const fd_t fd = evq->sig_fd[0];
    int set = 0;

    /* get triggered signal numbers from pipe */
    for (; ; ) {
	char buf[BUFSIZ];
	int n;

	do n = read(fd, buf, sizeof(buf));
	while (n == -1 && errno == EINTR);
	if (n <= 0)
	    return ev_ready;

	while (n--) {
	    const int signo = buf[n];
	    const int bit = 1 << signo;

	    if (!(set & bit)) {
		set |= bit;
		ev_ready = (signo == SIGCHLD)
		 ? signal_children(ev_ready, now)
		 : signal_actives(signo, ev_ready, now);
	    }
	}
    }
}
#endif
