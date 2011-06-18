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


#include EVQ_SOURCE

