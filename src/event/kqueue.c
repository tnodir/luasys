/* KQueue */

#include <sys/wait.h>

EVQ_API int
evq_init (struct event_queue *evq)
{
    evq->kqueue_fd = kqueue();
    if (evq->kqueue_fd == -1)
	return -1;

    pthread_mutex_init(&evq->cs, NULL);

    {
	fd_t *sig_fd = evq->sig_fd;
	struct kevent kev;

	memset(&kev, 0, sizeof(struct kevent));
	kev.filter = EVFILT_READ;
	kev.flags = EV_ADD;

	sig_fd[0] = sig_fd[1] = (fd_t) -1;
	if (pipe(sig_fd) || fcntl(sig_fd[0], F_SETFL, O_NONBLOCK))
	    goto err;

	kev.ident = sig_fd[0];
	if (kevent(evq->kqueue_fd, &kev, 1, NULL, 0, NULL))
	    goto err;
    }

    evq->now = sys_milliseconds();
    return 0;
 err:
    evq_done(evq);
    return -1;
}

EVQ_API void
evq_done (struct event_queue *evq)
{
    pthread_mutex_destroy(&evq->cs);

    close(evq->sig_fd[0]);
    close(evq->sig_fd[1]);

    close(evq->kqueue_fd);
}

static int
kqueue_set (struct event_queue *evq, struct event *ev, int filter, int action)
{
    struct kevent *kev = evq->kev_list;

    if (evq->nchanges >= NEVENT) {
	int res;

	do res = kevent(evq->kqueue_fd, kev, evq->nchanges, NULL, 0, NULL);
	while (res == -1 && errno == EINTR);

	if (res == -1) return -1;

	evq->nchanges = 1;
    } else
	kev += evq->nchanges++;

    memset(kev, 0, sizeof(struct kevent));
    kev->ident = ev->fd;
    kev->filter = filter;
    kev->flags = action | ((ev->flags & EVENT_ONESHOT) ? EV_ONESHOT : 0);
    kev->udata = (kev_udata_t) ev;
    return 0;
}

EVQ_API int
evq_add (struct event_queue *evq, struct event *ev)
{
    const unsigned int ev_flags = ev->flags;

    ev->evq = evq;

    if (ev_flags & EVENT_SIGNAL)
	return signal_add(evq, ev);

    if ((ev_flags & EVENT_READ)
     && kqueue_set(evq, ev, EVFILT_READ, EV_ADD))
	return -1;

    if ((ev_flags & EVENT_WRITE)
     && kqueue_set(evq, ev, EVFILT_WRITE, EV_ADD))
	return -1;

    evq->nevents++;
    return 0;
}

EVQ_API int
evq_add_dirwatch (struct event_queue *evq, struct event *ev, const char *path)
{
    const int flags = NOTE_DELETE | NOTE_WRITE | NOTE_EXTEND | NOTE_ATTRIB
     | NOTE_LINK | NOTE_RENAME | NOTE_REVOKE;

    const unsigned int filter = (ev->flags >> EVENT_EOF_SHIFT_RES)
     ? NOTE_WRITE : flags;

    ev->flags &= ~EVENT_EOF_MASK_RES;
    ev->evq = evq;

    ev->fd = open(path, O_RDONLY);
    if (ev->fd == -1) return -1;

    {
	struct kevent kev;
	int res;

	memset(&kev, 0, sizeof(struct kevent));
	kev.ident = ev->fd;
	kev.filter = EVFILT_VNODE;
	kev.flags = EV_ADD;
	kev.fflags = filter;
	kev.udata = (kev_udata_t) ev;

	do res = kevent(evq->kqueue_fd, &kev, 1, NULL, 0, NULL);
	while (res == -1 && errno == EINTR);

	if (res == -1) {
	    close(ev->fd);
	    return -1;
	}
    }

    evq->nevents++;
    return 0;
}

EVQ_API int
evq_del (struct event *ev, const int reuse_fd)
{
    struct event_queue *evq = ev->evq;
    const unsigned int ev_flags = ev->flags;

    if (ev->tq) timeout_del(ev);

    ev->evq = NULL;
    evq->nevents--;

    if (ev_flags & EVENT_TIMER) return 0;

    if (ev_flags & EVENT_SIGNAL)
	return signal_del(evq, ev);

    if (ev_flags & EVENT_DIRWATCH)
	return close(ev->fd);

    if (!reuse_fd) return 0;

    return ((ev_flags & EVENT_READ)
	? kqueue_set(evq, ev, EVFILT_READ, EV_DELETE) : 0)
     | ((ev_flags & EVENT_WRITE)
	? kqueue_set(evq, ev, EVFILT_WRITE, EV_DELETE) : 0);
}

EVQ_API int
evq_modify (struct event *ev, unsigned int flags)
{
    struct event_queue *evq = ev->evq;
    const unsigned int ev_flags = ev->flags;

    if (ev_flags & EVENT_READ) {
	if (flags & EVENT_READ)
	    flags &= ~EVENT_READ;
	else
	    if (kqueue_set(evq, ev, EVFILT_READ, EV_DELETE))
		return -1;
    }
    if (ev_flags & EVENT_WRITE) {
	if (flags & EVENT_WRITE)
	    flags &= ~EVENT_WRITE;
	else
	    if (kqueue_set(evq, ev, EVFILT_WRITE, EV_DELETE))
		return -1;
    }

    if ((flags & EVENT_READ)
     && kqueue_set(evq, ev, EVFILT_READ, EV_ADD))
	return -1;

    if ((flags & EVENT_WRITE)
     && kqueue_set(evq, ev, EVFILT_WRITE, EV_ADD))
	return -1;

    return 0;
}

EVQ_API int
evq_wait (struct event_queue *evq, msec_t timeout)
{
    struct event *ev_ready;
    struct kevent *kev = evq->kev_list;
    struct timespec ts, *tsp;
    int nready;

    if (timeout != 0L) {
	timeout = timeout_get(evq->tq, timeout, evq->now);
	if (timeout == 0L) {
	    ev_ready = timeout_process(evq->tq, NULL, evq->now);
	    goto end;
	}
    }
    if (timeout == TIMEOUT_INFINITE)
	tsp = NULL;
    else {
	ts.tv_sec = timeout / 1000;
	ts.tv_nsec = (timeout % 1000) * 1000000;
	tsp = &ts;
    }

    sys_vm_leave();
    nready = kevent(evq->kqueue_fd, kev, evq->nchanges, kev, NEVENT, tsp);
    sys_vm_enter();

    evq->nchanges = 0;
    evq->now = sys_milliseconds();

    if (nready == -1)
	return (errno == EINTR) ? 0 : EVQ_FAILED;

    if (tsp) {
	if (!nready) {
	    ev_ready = !evq->tq ? NULL
	     : timeout_process(evq->tq, NULL, evq->now);
	    if (ev_ready) goto end;
	    return EVQ_TIMEOUT;
	}

	timeout = evq->now;
    }

    ev_ready = NULL;
    for (; nready--; ++kev) {
	struct event *ev;
	const int flags = kev->flags;
	const int filter = kev->filter;

	if (flags & EV_ERROR)
	    continue;

	if (filter == EVFILT_SIGNAL) {
	    ev_ready = signal_process_actives(evq, kev->ident,
	     ev_ready, timeout);
	    continue;
	}

	ev = (struct event *) kev->udata;
	if (!ev) {
	    ev_ready = signal_process_interrupt(evq, ev_ready, timeout);
	    continue;
	}

	ev->flags |= (filter == EVFILT_READ ? EVENT_READ_RES : EVENT_WRITE_RES)
	 | ((flags & EV_EOF) ? EVENT_EOF_RES : 0);

	if (ev->flags & EVENT_ACTIVE)
	    continue;

	ev->flags |= EVENT_ACTIVE;
	if (ev->flags & EVENT_ONESHOT)
	    evq_del(ev, 1);
	else if (ev->tq && !(ev->flags & EVENT_TIMEOUT_MANUAL))
	    timeout_reset(ev, timeout);

	ev->next_ready = ev_ready;
	ev_ready = ev;
    }
    if (!ev_ready) return 0;
 end:
    evq->ev_ready = ev_ready;
    return 0;
}

