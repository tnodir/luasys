/* Win32 Console Signals */

/* Global signal events */
static struct event *sig_events[NSIG];
static int volatile sig_ignore;  /* ignored signals */

/* References count do not trace */
#define signal_set(add)	(!SetConsoleCtrlHandler((PHANDLER_ROUTINE) signal_handler, (add)))

static int
signal_handler (int signo)
{
    const int bit = 1 << signo;
    struct event *ev;

    if (sig_ignore & bit)
	return 1;

    ev = sig_events[signo];
    if (ev) {
	struct win32thr *wth = ev->wth;

	EnterCriticalSection(&wth->cs);
	wth->evq->sig_ready |= bit;
	SetEvent(wth->signal);
	LeaveCriticalSection(&wth->cs);
	return 1;
    }
    return 0;
}

int
evq_ignore_signal (struct event_queue *evq, int signo, int ignore)
{
    const int bit = 1 << signo;

    (void) evq;

    if (!sig_ignore && signal_set(1))
	return -1;

    if (ignore)
	sig_ignore |= bit;
    else
	sig_ignore &= ~bit;
    return 0;
}

static int
signal_add (struct event_queue *evq, struct event *ev)
{
    const int signo = (int) ev->fd;
    struct event **sig_evp = &sig_events[signo];

    if (*sig_evp)
	ev->next_object = *sig_evp;
    else {
	if (signal_set(1))
	    return -1;
	ev->next_object = NULL;
    }
    *sig_evp = ev;
    sig_ignore &= ~(1 << signo);  /* don't ignore signal */

    evq->nevents++;
    return 0;
}

static int
signal_del (struct event *ev)
{
    const int signo = (int) ev->fd;
    struct event **sig_evp = &sig_events[signo];

    if (*sig_evp == ev) {
	if (!(*sig_evp = ev->next_object)
	 && !sig_ignore && signal_set(0))
	    return -1;
    } else {
	struct event *sig_ev = *sig_evp;

	while (sig_ev->next_object != ev)
	    sig_ev = sig_ev->next_object;
	sig_ev->next_object = ev->next_object;
    }
    return 0;
}

static struct event *
signal_active (struct event *ev, struct event *ev_ready, msec_t timeout)
{
    ev->flags |= EVENT_ACTIVE | EVENT_READ_RES;
    if (ev->flags & EVENT_ONESHOT)
	evq_del(ev, 1);
    else if (ev->tq)
	timeout_reset(ev, timeout);

    ev->next_ready = ev_ready;
    return ev;
}

static struct event *
signal_process (unsigned int sig_ready, struct event *ev_ready, msec_t timeout)
{
    int signo;

    for (signo = 0; sig_ready; ++signo, sig_ready >>= 1)
	if (sig_ready & 1) {
	    struct event *ev = sig_events[signo];

	    do {
		ev_ready = signal_active(ev, ev_ready, timeout);
		ev = ev->next_object;
	    } while (ev != NULL);
	}
    return ev_ready;
}

