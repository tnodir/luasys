/* Poll */

#define POLLFD_READ	(POLLIN | POLLERR | POLLHUP | POLLNVAL)
#define POLLFD_WRITE	(POLLOUT | POLLERR | POLLHUP | POLLNVAL)


EVQ_API int
evq_init (struct event_queue *evq)
{
  evq->events = malloc(NEVENT * sizeof(void *));
  if (!evq->events)
    return -1;

  evq->fdset = malloc(NEVENT * sizeof(struct pollfd));
  if (!evq->fdset) {
    free(evq->events);
    return -1;
  }

  pthread_mutex_init(&evq->sig_cs, NULL);

  {
    fd_t *sig_fd = evq->sig_fd;
    struct pollfd *fdp;

    sig_fd[0] = sig_fd[1] = (fd_t) -1;
    if (pipe(sig_fd) || fcntl(sig_fd[0], F_SETFL, O_NONBLOCK))
      goto err;

    fdp = &evq->fdset[0];
    fdp->fd = sig_fd[0];
    fdp->events = POLLIN;
    fdp->revents = 0;
  }

  evq->npolls++;
  evq->max_polls = NEVENT;

  evq->now = sys_milliseconds();
  return 0;
 err:
  evq_done(evq);
  return -1;
}

EVQ_API void
evq_done (struct event_queue *evq)
{
  pthread_mutex_destroy(&evq->sig_cs);

  close(evq->sig_fd[0]);
  close(evq->sig_fd[1]);

  free(evq->fdset);
  free(evq->events);
}

EVQ_API int
evq_add (struct event_queue *evq, struct event *ev)
{
  unsigned int npolls;

  ev->evq = evq;

  if (ev->flags & EVENT_SIGNAL)
    return signal_add(evq, ev);

  npolls = evq->npolls;
  if (npolls >= evq->max_polls) {
    const unsigned int n = 2 * evq->max_polls;
    void *p;

    if (!(p = realloc(evq->events, n * sizeof(void *))))
      return -1;
    evq->events = p;

    if (!(p = realloc(evq->fdset, n * sizeof(struct pollfd))))
      return -1;
    evq->fdset = p;
    evq->max_polls = n;
  }

  {
    struct pollfd *fdp = &evq->fdset[npolls];

    fdp->fd = ev->fd;
    fdp->events = (ev->flags & EVENT_READ) ? POLLIN : POLLOUT;
    fdp->revents = 0;
  }

  evq->npolls++;
  evq->events[npolls] = ev;
  ev->index = npolls;

  evq->nevents++;
  return 0;
}

EVQ_API int
evq_add_dirwatch (struct event_queue *evq, struct event *ev, const char *path)
{
  (void) evq;
  (void) ev;
  (void) path;

  return -1;
}

EVQ_API int
evq_del (struct event *ev, int reuse_fd)
{
  struct event_queue *evq = ev->evq;
  const unsigned int ev_flags = ev->flags;
  unsigned int npolls;
  int i;

  (void) reuse_fd;

  if (ev->tq) timeout_del(ev);

  ev->evq = NULL;
  evq->nevents--;

  if (ev_flags & EVENT_TIMER) return 0;

  if (ev_flags & EVENT_SIGNAL)
    return signal_del(evq, ev);

  npolls = --evq->npolls;
  if (ev->index < npolls) {
    struct event **events = evq->events;

    i = ev->index;
    events[i] = events[npolls];
    events[i]->index = i;
    evq->fdset[i] = evq->fdset[npolls];
  }

  if (npolls > NEVENT / 2 && npolls <= evq->max_polls / 4) {
    void *p;

    i = (evq->max_polls /= 2);
    if ((p = realloc(evq->events, i * sizeof(void *))))
      evq->events = p;
    if ((p = realloc(evq->fdset, i * sizeof(struct pollfd))))
      evq->fdset = p;
  }
  return 0;
}

EVQ_API int
evq_modify (struct event *ev, unsigned int flags)
{
  short *eventp = &ev->evq->fdset[ev->index].events;

  *eventp = (flags & EVENT_READ) ? POLLIN : POLLOUT;
  return 0;
}

EVQ_API int
evq_wait (struct event_queue *evq, struct sys_thread *td, msec_t timeout)
{
  struct event *ev_ready;
  struct event **events = evq->events;
  struct pollfd *fdset = evq->fdset;
  const int npolls = evq->npolls;
  int i, nready;

  if (timeout != 0L) {
    timeout = timeout_get(evq->tq, timeout, evq->now);
    if (timeout == 0L) {
      ev_ready = timeout_process(evq->tq, NULL, evq->now);
      goto end;
    }
  }

  if (td) sys_vm2_leave(td);
  nready = poll(fdset, npolls, (int) timeout);
  if (td) sys_vm2_enter(td);

  evq->now = sys_milliseconds();

  if (nready == -1)
    return (errno == EINTR) ? 0 : -1;

  ev_ready = evq->ev_ready;
  if (timeout != TIMEOUT_INFINITE) {
    if (!nready) {
      if (evq->tq) {
        struct event *ev = timeout_process(evq->tq, ev_ready, evq->now);
        if (ev != ev_ready) {
          ev_ready = ev;
          goto end;
        }
      }
      return SYS_ERR_TIMEOUT;
    }

    timeout = evq->now;
  }

  if (fdset[0].revents & POLLIN) {
    fdset[0].revents = 0;
    ev_ready = signal_process_interrupt(evq, ev_ready, timeout);
    --nready;
  }

  for (i = 1; i < npolls && nready; i++) {
    const int revents = fdset[i].revents;
    struct event *ev;
    unsigned int res;

    if (!revents) continue;

    fdset[i].revents = 0;
    ev = events[i];

    res = (revents & POLLHUP) ? EVENT_EOF_RES : 0;
    if ((revents & POLLFD_READ) && (ev->flags & EVENT_READ))
      res |= EVENT_READ_RES;
    else if ((revents & POLLFD_WRITE) && (ev->flags & EVENT_WRITE))
      res |= EVENT_WRITE_RES;

    ev->flags |= res;
    if (!(ev->flags & EVENT_ACTIVE)) {
      ev->flags |= EVENT_ACTIVE;
      if (ev->flags & EVENT_ONESHOT)
        evq_del(ev, 1);
      else if (ev->tq && !(ev->flags & EVENT_TIMEOUT_MANUAL))
        timeout_reset(ev, timeout);

      ev->next_ready = ev_ready;
      ev_ready = ev;
    }
    --nready;
  }
  if (!ev_ready) return 0;
 end:
  evq->ev_ready = ev_ready;
  return 0;
}

