/* Signals */

#include <sys/wait.h>


/* Global signal events */
static struct {
  pthread_mutex_t cs;
  struct event *events[EVQ_NSIG];
} g_Signal;
static int volatile g_SignalInit = 0;


static struct event **
signal_gethead (int signo)
{
  switch (signo) {
  case EVQ_SIGINT: signo = 0; break;
  case EVQ_SIGQUIT: signo = 1; break;
  case EVQ_SIGHUP: signo = 2; break;
  case EVQ_SIGTERM: signo = 3; break;
  case EVQ_SIGCHLD: signo = 4; break;
  default: return NULL;
  }
  return &g_Signal.events[signo];
}

static void
signal_handler (const int signo)
{
#ifdef USE_KQUEUE
  (void) signo;
#else
  struct event **sig_evp;

  if (signo == SYS_SIGINTR) return;

  sig_evp = signal_gethead(signo);
  if (!sig_evp) return;

  pthread_mutex_lock(&g_Signal.cs);
  {
    struct event *ev = *sig_evp;
    for (; ev; ev = ev->next_object) {
      if (!event_deleted(ev))
        evq_signal(ev->evq, signo);
    }
  }
  pthread_mutex_unlock(&g_Signal.cs);
#endif
}

EVQ_API void
signal_init (void)
{
  if (g_SignalInit) return;
  g_SignalInit = 1;

  /* Initialize critical section */
  pthread_mutex_init(&g_Signal.cs, NULL);
  /* Ignore sigpipe or it will crash us */
  signal_set(SIGPIPE, SIG_IGN);
  /* To interrupt blocking syscalls */
  signal_set(SYS_SIGINTR, signal_handler);
}

static int
evq_interrupt (struct event_queue *evq)
{
#if defined(USE_EPOLL) && defined(USE_EVENTFD)
  const fd_t fd = evq->sig_fd[0];
  const int64_t data = 1;
#else
  const fd_t fd = evq->sig_fd[1];
  const char data = 0;
#endif
  int nw;

  do nw = write(fd, &data, sizeof(data));
  while (nw == -1 && errno == EINTR);

  return (nw == -1) ? -1 : 0;
}

EVQ_API int
evq_signal (struct event_queue *evq, const int signo)
{
  int res = 0;

  pthread_mutex_lock(&evq->sig_cs);
  if (!evq->sig_ready)
    res = evq_interrupt(evq);
  evq->sig_ready |= 1 << signo;
  pthread_mutex_unlock(&evq->sig_cs);
  return res;
}

EVQ_API int
signal_set (const int signo, sig_handler_t func)
{
  struct sigaction act;
  int res;

  act.sa_handler = func;
  sigemptyset(&act.sa_mask);
  act.sa_flags = (signo == SYS_SIGINTR) ? 0 : SA_RESTART;

  do res = sigaction(signo, &act, NULL);
  while (res == -1 && errno == EINTR);

  return res;
}

#ifdef USE_KQUEUE
static int
signal_kqueue (struct event_queue *evq, const int signo, const int action)
{
  struct kevent kev;
  int res;

  memset(&kev, 0, sizeof(struct kevent));
  kev.ident = signo;
  kev.filter = EVFILT_SIGNAL;
  kev.flags = action;

  do res = kevent(evq->kqueue_fd, &kev, 1, NULL, 0, NULL);
  while (res == -1 && errno == EINTR);

  return res;
}
#endif

EVQ_API int
evq_ignore_signal (struct event_queue *evq, const int signo, const int ignore)
{
#ifndef USE_KQUEUE
  (void) evq;
#else
  if (signal_kqueue(evq, signo, ignore ? EV_DELETE : EV_ADD))
    return -1;
#endif
  if (ignore)
    return signal_set(signo, SIG_IGN);
  else {
    struct event **sig_evp = signal_gethead(signo);
    int res;

    pthread_mutex_lock(&g_Signal.cs);
    res = signal_set(signo,
     (sig_evp && *sig_evp ? signal_handler : SIG_DFL));
    pthread_mutex_unlock(&g_Signal.cs);
    return res;
  }
}

static int
signal_add (struct event_queue *evq, struct event *ev)
{
  const int signo = (ev->flags & EVENT_PID) ? EVQ_SIGCHLD : (int) ev->fd;
  struct event **sig_evp = signal_gethead(signo);

  if (!sig_evp) return -1;

  pthread_mutex_lock(&g_Signal.cs);
  if (*sig_evp)
    ev->next_object = *sig_evp;
  else {
#ifdef USE_KQUEUE
    if (signal_kqueue(evq, signo, EV_ADD))
      goto err;
#endif
    if (signal_set(signo, signal_handler))
      goto err;
    ev->next_object = NULL;
  }
  *sig_evp = ev;
  pthread_mutex_unlock(&g_Signal.cs);

  evq->nevents++;
  return 0;
 err:
  pthread_mutex_unlock(&g_Signal.cs);
  return -1;
}

static int
signal_del (struct event_queue *evq, struct event *ev)
{
  const int signo = (ev->flags & EVENT_PID) ? EVQ_SIGCHLD
   : (int) ev->fd;
  struct event **sig_evp = signal_gethead(signo);
  int res = 0;

  pthread_mutex_lock(&g_Signal.cs);
  if (*sig_evp == ev) {
    if (!(*sig_evp = ev->next_object)) {
#ifndef USE_KQUEUE
      (void) evq;
#else
      res |= signal_kqueue(evq, signo, EV_DELETE);
#endif
      res |= signal_set(signo, SIG_DFL);
    }
  } else {
    struct event *sig_ev = *sig_evp;

    while (sig_ev->next_object != ev)
      sig_ev = sig_ev->next_object;
    sig_ev->next_object = ev->next_object;
  }
  pthread_mutex_unlock(&g_Signal.cs);
  return res;
}

static int
signal_process_child (struct event *ev)
{
  const int fd = (int) ev->fd;
  int pid, status;

  do pid = waitpid(fd, &status, WNOHANG);
  while (pid == -1 && errno == EINTR);

  if (pid == fd) {
    ev->flags |= !WIFEXITED(status) ? EVENT_STATUS_MASK
     : ((unsigned int) WEXITSTATUS(status) << EVENT_STATUS_SHIFT);
    return 0;
  }
  return -1;
}

static struct event *
signal_process_actives (struct event_queue *evq, const int signo,
                        struct event *ev_ready, const msec_t now)
{
  struct event **sig_evp = signal_gethead(signo);
  struct event *ev, *ev_next = NULL;

  pthread_mutex_lock(&g_Signal.cs);
  ev = *sig_evp;
  for (; ev; ev = ev->next_object) {
    if (ev->evq == evq && !(ev->flags & EVENT_ACTIVE)) {
      ev->next_ready = ev_next;
      ev_next = ev;
    }
  }
  pthread_mutex_unlock(&g_Signal.cs);

  while (ev_next) {
    ev = ev_next;
    ev_next = ev->next_ready;
    if (signo == EVQ_SIGCHLD && signal_process_child(ev))
      continue;
    ev_ready = evq_process_active(ev, ev_ready, now);
  }
  return ev_ready;
}

static struct event *
signal_process_interrupt (struct event_queue *evq, struct event *ev_ready,
                          const msec_t now)
{
  unsigned int sig_ready;
  int signo;

  pthread_mutex_lock(&evq->sig_cs);
  /* reset interruption event */
  {
    const fd_t fd = evq->sig_fd[0];
    char buf[8];  /* USE_EVENTFD: 8 bytes required */
    int nr;

    do nr = read(fd, buf, sizeof(buf));
    while (nr == -1 && errno == EINTR);
  }
  sig_ready = evq->sig_ready;
  evq->sig_ready = 0;
  pthread_mutex_unlock(&evq->sig_cs);

  sig_ready &= ~(1 << EVQ_SIGEVQ);

  for (signo = 0; sig_ready; ++signo, sig_ready >>= 1) {
    if (sig_ready & 1)
      ev_ready = signal_process_actives(evq, signo, ev_ready, now);
  }
  return ev_ready;
}

