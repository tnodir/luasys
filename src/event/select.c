/* Generic Select */

EVQ_API int
evq_init (struct event_queue *evq)
{
  pthread_mutex_init(&evq->sig_cs, NULL);

  {
    fd_t *sig_fd = evq->sig_fd;
    unsigned int fd;

    sig_fd[0] = sig_fd[1] = (fd_t) -1;
    if (pipe(sig_fd) || fcntl(sig_fd[0], F_SETFL, O_NONBLOCK))
      goto err;

    fd = (unsigned int) sig_fd[0];
    FD_SET(fd, &evq->readset);
    evq->max_fd = fd;
    evq->npolls++;
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
  pthread_mutex_destroy(&evq->sig_cs);

  close(evq->sig_fd[0]);
  close(evq->sig_fd[1]);
}

EVQ_API int
evq_add (struct event_queue *evq, struct event *ev)
{
  unsigned int fd;

  ev->evq = evq;

  if (ev->flags & EVENT_SIGNAL)
    return signal_add(evq, ev);

  if (evq->npolls >= FD_SETSIZE)
    return -1;

  fd = (unsigned int) ev->fd;
  if (ev->flags & EVENT_READ)
    FD_SET(fd, &evq->readset);
  else
    FD_SET(fd, &evq->writeset);

  if ((int) evq->max_fd != -1 && evq->max_fd < fd)
    evq->max_fd = fd;

  evq->events[evq->npolls] = ev;
  ev->index = evq->npolls++;

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

  (void) reuse_fd;

  if (ev->tq) timeout_del(ev);

  ev->evq = NULL;
  evq->nevents--;

  if (ev_flags & EVENT_TIMER) return 0;

  if (ev_flags & EVENT_SIGNAL)
    return signal_del(evq, ev);

  {
    const unsigned int fd = (unsigned int) ev->fd;

    if (ev_flags & EVENT_READ)
      FD_CLR(fd, &evq->readset);
    else
      FD_CLR(fd, &evq->writeset);

    if (evq->max_fd == fd)
      evq->max_fd = -1;
  }

  if (ev->index < --evq->npolls) {
    struct event **events = evq->events;
    const int i = ev->index;

    events[i] = events[evq->npolls];
    events[i]->index = i;
  }
  return 0;
}

EVQ_API int
evq_modify (struct event *ev, unsigned int flags)
{
  struct event_queue *evq = ev->evq;
  const unsigned int fd = (unsigned int) ev->fd;

  if (ev->flags & EVENT_READ)
    FD_CLR(fd, &evq->readset);
  else
    FD_CLR(fd, &evq->writeset);

  if (flags & EVENT_READ)
    FD_SET(fd, &evq->readset);
  else
    FD_SET(fd, &evq->writeset);

  return 0;
}

EVQ_API int
evq_wait (struct event_queue *evq, struct sys_thread *td, msec_t timeout)
{
  struct event *ev_ready;
  fd_set work_readset = evq->readset;
  fd_set work_writeset = evq->writeset;
  struct timeval tv, *tvp;
  struct event **events = evq->events;
  const int npolls = evq->npolls;
  int i, nready, max_fd;

  if (timeout != 0L) {
    timeout = timeout_get(evq->tq, timeout, evq->now);
    if (timeout == 0L) {
      ev_ready = timeout_process(evq->tq, NULL, evq->now);
      goto end;
    }
  }
  if (timeout == TIMEOUT_INFINITE)
    tvp = NULL;
  else {
    tv.tv_sec = timeout / 1000;
    tv.tv_usec = (timeout % 1000) * 1000;
    tvp = &tv;
  }

  max_fd = evq->max_fd;
  if (max_fd == -1) {
    for (i = 1; i < npolls; ++i) {
      struct event *ev = events[i];
      if (max_fd < ev->fd)
        max_fd = ev->fd;
    }
    evq->max_fd = max_fd;
  }

  if (td) sys_vm2_leave(td);
  nready = select(max_fd + 1, &work_readset, &work_writeset, NULL, tvp);
  if (td) sys_vm2_enter(td);

  evq->now = sys_milliseconds();

  if (nready == -1)
    return (errno == EINTR) ? 0 : -1;

  ev_ready = evq->ev_ready;
  if (tvp) {
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

  if (FD_ISSET(evq->sig_fd[0], &work_readset)) {
    ev_ready = signal_process_interrupt(evq, ev_ready, timeout);
    --nready;
  }

  for (i = 1; i < npolls && nready; i++) {
    struct event *ev = events[i];
    unsigned int res = 0;

    if ((ev->flags & EVENT_READ) && FD_ISSET(ev->fd, &work_readset))
      res |= EVENT_READ_RES;
    else if ((ev->flags & EVENT_WRITE) && FD_ISSET(ev->fd, &work_writeset))
      res |= EVENT_WRITE_RES;

    if (!res) continue;

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

