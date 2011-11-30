/* Event queue */

#include "evq.h"


#include "timeout.c"

#ifdef _WIN32

#include "win32sig.c"

#else

#include "signal.c"

int
evq_set_timeout (struct event *ev, msec_t msec)
{
    struct event_queue *evq = ev->evq;

    if (ev->tq) {
	if (ev->tq->msec == msec) {
	    timeout_reset(ev, evq->now);
	    return 0;
	}
	timeout_del(ev);
	if (msec == TIMEOUT_INFINITE)
	    return 0;
    }

    return timeout_add(ev, msec, evq->now);
}

int
evq_add_timer (struct event_queue *evq, struct event *ev, msec_t msec)
{
    ev->evq = evq;
    if (!evq_set_timeout(ev, msec)) {
	evq->nevents++;
	return 0;
    }
    return -1;
}

#endif /* !WIN32 */

int
evq_notify (struct event *ev, unsigned int flags)
{
    struct event_queue *evq = event_get_evq(ev);
    const unsigned int ev_flags = ev->flags;
    unsigned int res;

    res = ((ev_flags & EVENT_READ) ? (flags & EVENT_READ_RES) : 0)
     | ((ev_flags & EVENT_WRITE) ? (flags & EVENT_WRITE_RES) : 0)
     | (flags & (EVENT_EOF_RES | EVENT_ONESHOT));
    ev->flags |= res;

    if (!res || (ev_flags & EVENT_ACTIVE))
	return 0;
    ev->flags |= EVENT_ACTIVE;

    if (ev->flags & EVENT_ONESHOT)
	evq_del(ev, 0);
    else if (ev->tq && !(ev->flags & EVENT_TIMEOUT_MANUAL)) {
	evq_set_timeout(ev, ev->tq->msec);  /* timeout_reset */
    }

    ev->next_ready = evq->ev_ready;
    evq->ev_ready = ev;

    return evq_interrupt(evq);
}


#include EVQ_SOURCE

