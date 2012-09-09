/* Event queue */

#include "evq.h"


#include "timeout.c"

#ifdef _WIN32

#include "win32sig.c"

#else

#include "signal.c"

EVQ_API int
evq_set_timeout (struct event *ev, const msec_t msec)
{
  struct event_queue *evq = ev->evq;

  if (ev->tq) {
    if (ev->tq->msec == msec) {
      timeout_reset(ev, evq->now);
      return 0;
    }
    timeout_del(ev);
  }

  return (msec == TIMEOUT_INFINITE) ? 0
   : timeout_add(ev, msec, evq->now);
}

EVQ_API int
evq_add_timer (struct event_queue *evq, struct event *ev, const msec_t msec)
{
  ev->evq = evq;
  if (!evq_set_timeout(ev, msec)) {
    evq->nevents++;
    return 0;
  }
  return -1;
}

#endif /* !WIN32 */

EVQ_API struct event *
evq_process_active (struct event *ev, struct event *ev_ready,
                    const msec_t now)
{
  ev->flags |= EVENT_READ_RES;
  if (ev->flags & EVENT_ACTIVE)
    return ev_ready;

  ev->flags |= EVENT_ACTIVE;
  if (ev->flags & EVENT_ONESHOT)
    evq_del(ev, 1);
  else if (ev->tq && !(ev->flags & EVENT_TIMEOUT_MANUAL))
    timeout_reset(ev, now);

  ev->next_ready = ev_ready;
  return ev;
}


#include EVQ_SOURCE

