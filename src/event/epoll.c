/* EPoll */

#define EPOLLFD_READ	(EPOLLIN | EPOLLERR | EPOLLHUP)
#define EPOLLFD_WRITE	(EPOLLOUT | EPOLLERR | EPOLLHUP)

EVQ_API int
evq_init (struct event_queue *evq)
{
    evq->epoll_fd = epoll_create(NEVENT);
    if (evq->epoll_fd == -1)
	return -1;

    pthread_mutex_init(&evq->cs, NULL);

    {
	fd_t *sig_fd = evq->sig_fd;
	struct epoll_event epev;

	memset(&epev, 0, sizeof(struct epoll_event));
	epev.events = EPOLLIN;

#ifdef USE_EVENTFD
	sig_fd[0] = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
	if (sig_fd[0] == -1)
	    goto err;
#else
	sig_fd[0] = sig_fd[1] = (fd_t) -1;
	if (pipe(sig_fd) || fcntl(sig_fd[0], F_SETFL, O_NONBLOCK))
	    goto err;
#endif

	if (epoll_ctl(evq->epoll_fd, EPOLL_CTL_ADD, sig_fd[0], &epev))
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

#ifdef USE_EVENTFD
    close(evq->sig_fd[0]);
#else
    close(evq->sig_fd[0]);
    close(evq->sig_fd[1]);
#endif

    close(evq->epoll_fd);
}

EVQ_API int
evq_add (struct event_queue *evq, struct event *ev)
{
    const unsigned int ev_flags = ev->flags;

    ev->evq = evq;

    if (ev_flags & EVENT_SIGNAL)
	return signal_add(evq, ev);

    {
	struct epoll_event epev;

	memset(&epev, 0, sizeof(struct epoll_event));
	if (ev_flags & EVENT_READ)
	    epev.events = EPOLLIN;
	if (ev_flags & EVENT_WRITE)
	    epev.events |= EPOLLOUT;
	epev.events |= (ev_flags & EVENT_ONESHOT) ? EPOLLONESHOT : 0;
	epev.data.ptr = ev;
	if (epoll_ctl(evq->epoll_fd, EPOLL_CTL_ADD, ev->fd, &epev) == -1)
	    return -1;
    }

    evq->nevents++;
    return 0;
}

EVQ_API int
evq_add_dirwatch (struct event_queue *evq, struct event *ev, const char *path)
{
    const unsigned int filter = (ev->flags >> EVENT_EOF_SHIFT_RES)
     ? IN_MODIFY : IN_ALL_EVENTS ^ IN_ACCESS;

    ev->flags &= ~EVENT_EOF_MASK_RES;

    ev->fd = inotify_init();
    if (ev->fd == -1) return -1;

    if (inotify_add_watch(ev->fd, path, filter) == -1) {
	close(ev->fd);
	return -1;
    }

    return evq_add(evq, ev);
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

    if (reuse_fd)
	epoll_ctl(evq->epoll_fd, EPOLL_CTL_DEL, ev->fd, NULL);
    return 0;
}

EVQ_API int
evq_modify (struct event *ev, unsigned int flags)
{
    struct epoll_event epev;

    memset(&epev, 0, sizeof(struct epoll_event));
    if (flags & EVENT_READ)
	epev.events = EPOLLIN;
    if (flags & EVENT_WRITE)
	epev.events |= EPOLLOUT;
    epev.data.ptr = ev;
    return epoll_ctl(ev->evq->epoll_fd, EPOLL_CTL_MOD, ev->fd, &epev);
}

EVQ_API int
evq_wait (struct event_queue *evq, msec_t timeout)
{
    struct epoll_event ep_events[NEVENT];
    struct epoll_event *epev;
    struct event *ev_ready;
    int nready;

    if (timeout != 0L) {
	timeout = timeout_get(evq->tq, timeout, evq->now);
	if (timeout == 0L) {
	    ev_ready = timeout_process(evq->tq, NULL, evq->now);
	    goto end;
	}
    }

    sys_vm_leave();
    nready = epoll_wait(evq->epoll_fd, ep_events, NEVENT, (int) timeout);
    sys_vm_enter();

    evq->now = sys_milliseconds();

    if (nready == -1)
	return (errno == EINTR) ? 0 : EVQ_FAILED;

    if (timeout != TIMEOUT_INFINITE) {
	if (!nready) {
	    ev_ready = !evq->tq ? NULL
	     : timeout_process(evq->tq, NULL, evq->now);
	    if (ev_ready) goto end;
	    return EVQ_TIMEOUT;
	}

	timeout = evq->now;
    }

    ev_ready = NULL;
    for (epev = ep_events; nready--; ++epev) {
	const int revents = epev->events;
	struct event *ev;
	unsigned int res;

	if (!revents) continue;

	ev = epev->data.ptr;
	if (!ev) {
	    ev_ready = signal_process_interrupt(evq, ev_ready, timeout);
	    continue;
	}

	res = EVENT_ACTIVE;
	if ((revents & EPOLLFD_READ) && (ev->flags & EVENT_READ)) {
	    res |= EVENT_READ_RES;

	    if (ev->flags & EVENT_DIRWATCH) {  /* skip inotify data */
		char buf[BUFSIZ];
		int n;
		do n = read(ev->fd, buf, sizeof(buf));
		while (n == -1 && errno == EINTR);
	    }
	}
	if ((revents & EPOLLFD_WRITE) && (ev->flags & EVENT_WRITE))
	    res |= EVENT_WRITE_RES;
	if (revents & EPOLLHUP)
	    res |= EVENT_EOF_RES;

	ev->flags |= res;
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

