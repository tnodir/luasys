/* Timeouts */

static void
timeout_reset (struct event *ev, const msec_t now)
{
  struct timeout_queue *tq = ev->tq;
  const msec_t msec = tq->msec;

  ev->timeout_at = now + msec;
  if (!ev->next)
    return;

  if (ev->prev)
    ev->prev->next = ev->next;
  else
    tq->ev_head = ev->next;

  ev->next->prev = ev->prev;
  ev->next = NULL;
  ev->prev = tq->ev_tail;
  tq->ev_tail->next = ev;
  tq->ev_tail = ev;
}

static void
timeout_del (struct event *ev)
{
  struct timeout_queue *tq = ev->tq;
  struct event *ev_prev, *ev_next;

  if (!tq) return;
  ev->tq = NULL;

  ev_prev = ev->prev;
  ev_next = ev->next;

  if (!ev_prev && !ev_next) {
    struct event_queue *evq = event_get_evq(ev);
    struct timeout_queue **tq_headp = &event_get_tq_head(ev);
    struct event **ev_freep = &event_get_evq(ev)->ev_free;
    struct timeout_queue *tq_prev = tq->tq_prev;
    struct timeout_queue *tq_next = tq->tq_next;

    if (tq_prev)
      tq_prev->tq_next = tq_next;
    else
      *tq_headp = tq_next;

    if (tq_next)
      tq_next->tq_prev = tq_prev;

    ((struct event *) tq)->next_ready = *ev_freep;
    *ev_freep = ((struct event *) tq);

    if (evq->tq_map_fn) {
      /* remove from map */
      (void) evq->tq_map_fn(evq, tq, tq->msec, 1);
    }
    return;
  }

  if (ev_prev)
    ev_prev->next = ev_next;
  else
    tq->ev_head = ev_next;

  if (ev_next)
    ev_next->prev = ev_prev;
  else
    tq->ev_tail = ev_prev;
}

static int
timeout_add (struct event *ev, msec_t msec, const msec_t now)
{
  struct event_queue *evq = event_get_evq(ev);
  struct timeout_queue **tq_headp = &event_get_tq_head(ev);
  struct timeout_queue *tq, *tq_prev;

  tq_prev = NULL;
  tq = *tq_headp;

  if (evq->tq_map_fn) {
    /* search from map */
    tq_prev = evq->tq_map_fn(evq, NULL, msec, 0);
    if (tq_prev) tq = tq_prev;
  } else {
    /* search from sorted list */
    for (; tq && tq->msec < msec; tq = tq->tq_next)
      tq_prev = tq;
  }

  if (!tq || tq->msec != msec) {
    struct event **ev_freep = &event_get_evq(ev)->ev_free;
    struct timeout_queue *tq_new = (struct timeout_queue *) *ev_freep;

    if (!tq_new) return -1;
    *ev_freep = (*ev_freep)->next_ready;

    tq_new->tq_next = tq;
    if (tq) tq->tq_prev = tq_new;
    tq = tq_new;
    tq->tq_prev = tq_prev;

    if (tq_prev)
      tq_prev->tq_next = tq;
    else
      *tq_headp = tq;

    tq->msec = msec;
    tq->ev_head = ev;
    ev->prev = NULL;

    if (evq->tq_map_fn) {
      /* add to map */
      (void) evq->tq_map_fn(evq, tq, msec, 0);
    }
  } else {
    ev->prev = tq->ev_tail;
    if (tq->ev_tail)
      tq->ev_tail->next = ev;
    else
      tq->ev_head = ev;
  }
  tq->ev_tail = ev;
  ev->next = NULL;
  ev->tq = tq;
  ev->timeout_at = now + msec;
  return 0;
}

static msec_t
timeout_get (const struct timeout_queue *tq, msec_t min, const msec_t now)
{
  if (!tq) return min;

  if (min == TIMEOUT_INFINITE)
    min = tq->ev_head->timeout_at;
  else min += now;

  do {
    const msec_t t = tq->ev_head->timeout_at;
    if ((long) t < (long) min)
      min = t;
    tq = tq->tq_next;
  } while (tq);

  {
    const long d = (long) min - (long) now;
    return (d < 0L) ? 0L
     : (d > (long) MAX_TIMEOUT ? (msec_t) MAX_TIMEOUT : (msec_t) d);
  }
}

static struct event *
timeout_process (struct timeout_queue *tq, struct event *ev_ready,
                 const msec_t now)
{
  const long timeout = (long) now + MIN_TIMEOUT;

  while (tq) {
    struct event *ev_head = tq->ev_head;

    if (ev_head && (long) ev_head->timeout_at < timeout) {
      struct event *ev = ev_head;
      const msec_t timeout_at = now + tq->msec;

      do {
        ev->flags |= EVENT_ACTIVE | EVENT_TIMEOUT_RES;
        ev->timeout_at = timeout_at;

        ev->next_ready = ev_ready;
        ev_ready = ev;
        ev = ev->next;
      } while (ev && (long) ev->timeout_at < timeout);

      if (ev) {
        /* recycle timeout queue */
        tq->ev_head = ev;  /* head */
        ev->prev = NULL;
        tq->ev_tail->next = ev_head;  /* middle */
        ev_head->prev = tq->ev_tail;
        tq->ev_tail = ev_ready;  /* tail */
        ev_ready->next = NULL;
      }
    }
    tq = tq->tq_next;
  }
  return ev_ready;
}

