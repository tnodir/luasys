/* Win32 Console Signals */

/* Global signal events */
static struct {
    CRITICAL_SECTION cs;
    int volatile ignore;  /* ignored signals */
    struct event *events[NSIG];
} g_Signal;
static int volatile g_SignalInit = 0;

/* References count do not trace */
#define signal_set(add)	(!SetConsoleCtrlHandler((PHANDLER_ROUTINE) signal_handler, (add)))


static int
signal_handler (int signo)
{
    int res = 0;

    EnterCriticalSection(&g_Signal.cs);
    if (g_Signal.ignore & (1 << signo))
	res = 1;
    else {
	struct event *ev = g_Signal.events[signo];
	for (; ev; ev = ev->next_object) {
	    if (!event_deleted(ev))
		res |= evq_signal(ev->wth->evq, signo) ? 0 : 1;
	}
    }
    LeaveCriticalSection(&g_Signal.cs);
    return res;
}

void
signal_init ()
{
    if (g_SignalInit) return;
    g_SignalInit = 1;

    InitCriticalSection(&g_Signal.cs);
}

int
evq_signal (struct event_queue *evq, int signo)
{
    struct win32thr *wth = &evq->head;

    EnterCriticalSection(&wth->cs);
    if (!evq->sig_ready)
	SetEvent(wth->signal);
    evq->sig_ready |= 1 << signo;
    LeaveCriticalSection(&wth->cs);
    return 0;
}

int
evq_ignore_signal (struct event_queue *evq, int signo, int ignore)
{
    const int bit = 1 << signo;
    int res;

    (void) evq;

    EnterCriticalSection(&g_Signal.cs);
    if (!g_Signal.ignore && signal_set(1))
	res = -1;
    else {
	if (ignore)
	    g_Signal.ignore |= bit;
	else
	    g_Signal.ignore &= ~bit;
	res = 0;
    }
    LeaveCriticalSection(&g_Signal.cs);
    return res;
}

static int
signal_add (struct event_queue *evq, struct event *ev)
{
    const int signo = (int) ev->fd;
    struct event **sig_evp;

    EnterCriticalSection(&g_Signal.cs);
    sig_evp = &g_Signal.events[signo];
    if (*sig_evp)
	ev->next_object = *sig_evp;
    else {
	if (signal_set(1))
	    goto err;
	ev->next_object = NULL;
    }
    *sig_evp = ev;
    g_Signal.ignore &= ~(1 << signo);  /* don't ignore signal */
    LeaveCriticalSection(&g_Signal.cs);

    evq->nevents++;
    return 0;
 err:
    LeaveCriticalSection(&g_Signal.cs);
    return -1;
}

static int
signal_del (struct event *ev)
{
    const int signo = (int) ev->fd;
    struct event **sig_evp;
    int res = 0;

    EnterCriticalSection(&g_Signal.cs);
    sig_evp = &g_Signal.events[signo];
    if (*sig_evp == ev) {
	if (!(*sig_evp = ev->next_object)
	 && !g_Signal.ignore && signal_set(0))
	    res = -1;
    } else {
	struct event *sig_ev = *sig_evp;

	while (sig_ev->next_object != ev)
	    sig_ev = sig_ev->next_object;
	sig_ev->next_object = ev->next_object;
    }
    LeaveCriticalSection(&g_Signal.cs);
    return res;
}

static struct event *
signal_process_active (struct event *ev, struct event *ev_ready, msec_t timeout)
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
signal_process_actives (struct event_queue *evq, int signo, struct event *ev_ready, msec_t now)
{
    struct event *ev;

    EnterCriticalSection(&g_Signal.cs);
    ev = g_Signal.events[signo];
    for (; ev; ev = ev->next_object) {
	if (ev->wth->evq == evq)
	    ev_ready = signal_process_active(ev, ev_ready, now);
    }
    LeaveCriticalSection(&g_Signal.cs);
    return ev_ready;
}

static struct event *
signal_process_interrupt (struct event_queue *evq, unsigned int sig_ready,
                          struct event *ev_ready, msec_t now)
{
    int signo;

    for (signo = 0; sig_ready; ++signo, sig_ready >>= 1) {
	if (sig_ready & 1)
	    ev_ready = signal_process_actives(evq, signo, ev_ready, now);
    }
    return ev_ready;
}

