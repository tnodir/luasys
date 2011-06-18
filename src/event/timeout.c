/* Timeouts */

static void
timeout_reset (struct event *ev, msec_t now)
{
    struct timeout_queue *tq = ev->tq;
    const msec_t msec = tq->msec;

    if (msec == TIMEOUT_INFINITE)
	return;

    ev->timeout_at = msec + now;
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
timeout_add (struct event *ev, msec_t msec, msec_t now)
{
    struct timeout_queue **tq_headp = &event_get_tq_head(ev);
    struct timeout_queue *tq, *tq_prev;

    tq_prev = NULL;
    for (tq = *tq_headp; tq && tq->msec < msec; tq = tq->tq_next)
	tq_prev = tq;

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
    ev->timeout_at = msec + now;
    return 0;
}

static msec_t
timeout_get (const struct timeout_queue *tq, msec_t min, msec_t now)
{
    int is_infinite = 0;

    if (!tq) return min;

    if (min == TIMEOUT_INFINITE)
	is_infinite = 1;
    else
	min += now;

    do {
	if (tq->msec != TIMEOUT_INFINITE) {
	    const msec_t t = tq->ev_head->timeout_at;
	    if (is_infinite) {
		is_infinite = 0;
		min = t;
	    } else if ((long) t < (long) min)
		min = t;
	}
	tq = tq->tq_next;
    } while (tq);

    if (is_infinite)
	return TIMEOUT_INFINITE;
    else {
	const long timeout = (long) min - (long) now;
	return (timeout < 0L) ? 0L : (msec_t) timeout;
    }
}

static struct event *
timeout_process (struct timeout_queue *tq, struct event *ev_ready, msec_t now)
{
    long timeout_at = (long) now + MIN_TIMEOUT;

    while (tq) {
	struct event *ev_head = tq->ev_head;

	if (ev_head) {
	    struct event *ev = ev_head;

	    while ((long) ev->timeout_at <= timeout_at) {
		ev->flags |= EVENT_ACTIVE | EVENT_TIMEOUT_RES;
		ev->timeout_at = tq->msec + now;

		ev->next_ready = ev_ready;
		ev_ready = ev;
		ev = ev->next;
		if (!ev) break;
	    }
	    if (ev && ev != ev_head) {
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

