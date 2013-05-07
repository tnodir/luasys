/* Win32 NT I/O Completion Routines */

#define IOCR_NENTRY	48  /* number of events in head to trigger IOCR */

#define win32iocr_apc_put(apc_fn,evq,param) \
    (QueueUserAPC((apc_fn), (evq)->iocr.h, (param)) ? 0 : -1)

#define win32iocr_signal(evq) \
    win32iocr_apc_put(win32iocr_apc_signal, (evq), 0)

#define win32iocr_list_put(ov_list,ov) \
  do { \
    (ov)->o.ov_next = (ov_list).ov_head; \
    if (!(ov_list).ov_head) \
      (ov_list).ov_tail = (ov); \
    (ov_list).ov_head = (ov); \
  } while (0)

#define win32iocr_list_move(ov_list,ov_dest) \
  do { \
    (ov_list).ov_tail->o.ov_next = (ov_dest); \
    (ov_dest) = (ov_list).ov_head; \
    (ov_list).ov_head = (ov_list).ov_tail = NULL; \
  } while (0)


/* Global Thread Local Storage Index */
static DWORD g_IOCR_TLSIndex = TLS_OUT_OF_INDEXES;


static struct win32overlapped *
win32iocr_overlapped_alloc (struct event_queue *evq)
{
  struct win32overlapped *ov;
  const int n = evq->ov_buf_nevents;
  const int buf_idx = evq->ov_buf_index;
  const int nmax = (1 << (buf_idx + WIN32OV_BUF_IDX));

  ov = evq->ov_buffers[buf_idx];
  if (ov) {
    ov += n;
    if (++evq->ov_buf_nevents >= nmax) {
      evq->ov_buf_nevents = 0;
      evq->ov_buf_index++;
    }
  } else {
    if (buf_idx >= WIN32OV_BUF_SIZE
     || !(ov = malloc(nmax * sizeof(struct win32overlapped))))
      return NULL;

    evq->ov_buffers[buf_idx] = ov;
    evq->ov_buf_nevents = 1;
  }
  return ov;
}

static struct win32overlapped *
win32iocr_overlapped_new (struct event_queue *evq)
{
  struct win32overlapped *ov = evq->ov_free;

  if (ov)
    evq->ov_free = ov->o.ov_next;
  else
    ov = win32iocr_overlapped_alloc(evq);

  if (ov) memset(ov, 0, sizeof(struct win32overlapped));
  return ov;
}

static void
win32iocr_overlapped_del (struct event_queue *evq, struct win32overlapped *ov)
{
  ov->o.ov_next = evq->ov_free;
  evq->ov_free = ov;
}


static void WINAPI
win32iocr_apc_signal (ULONG_PTR param)
{
  (void) param;
}

static void WINAPI
win32iocr_apc_cancel (ULONG_PTR fd)
{
  CancelIo((HANDLE) fd);
}

static void WINAPI
win32iocr_completion (DWORD err, DWORD n, struct win32overlapped *ov,
                      DWORD flags)
{
  struct win32iocr_thread *iocr_thr = TlsGetValue(g_IOCR_TLSIndex);

  (void) err;  /* OVERLAPPED.Internal */
  (void) n;
  (void) flags;

  win32iocr_list_put(iocr_thr->ov_list, ov);
}

static void
win32iocr_set_handle (struct win32iocr_thread *iocr_thr,
                      struct win32overlapped *ov)
{
  static WSABUF buf = {0, 0};

  struct event *ev = ov->e.ev;
  unsigned int rw_flags;
  sd_t sd;

  if (!ev) goto ready;

  sd = (sd_t) ev->fd;
  rw_flags = ov->ih.rw_flags;
  ov->ih.rw_flags = 0;

  if (rw_flags == EVENT_READ) {
    DWORD flags = 0;

    if (!WSARecv(sd, &buf, 1, NULL, &flags, (OVERLAPPED *) ov,
     (LPWSAOVERLAPPED_COMPLETION_ROUTINE) win32iocr_completion)
     || WSAGetLastError() == WSA_IO_PENDING)
      return;
  } else {
    if (!WSASend(sd, &buf, 1, NULL, 0, (OVERLAPPED *) ov,
     (LPWSAOVERLAPPED_COMPLETION_ROUTINE) win32iocr_completion)
     || WSAGetLastError() == WSA_IO_PENDING)
      return;
  }
  ov->il.err = WSAGetLastError();
 ready:
  win32iocr_list_put(iocr_thr->ov_list, ov);
}

static void
win32iocr_submit (struct event_queue *evq)
{
  CRITICAL_SECTION *head_cs = &evq->head.sig_cs;

  EnterCriticalSection(head_cs);
  win32iocr_list_move(evq->iocr.ov_list, evq->iocr.iocr_thr->ov_set);
  LeaveCriticalSection(head_cs);

  (void) win32iocr_signal(evq);
}

static DWORD WINAPI
win32iocr_wait (struct event_queue *evq)
{
  CRITICAL_SECTION *head_cs = &evq->head.sig_cs;
  HANDLE head_signal = evq->head.signal;
  struct win32iocr_thread iocr_thr;

  memset(&iocr_thr, 0, sizeof(struct win32iocr_thread));
  TlsSetValue(g_IOCR_TLSIndex, &iocr_thr);

  evq->iocr.iocr_thr = &iocr_thr;
  SetEvent(evq->ack_event);

  while (!iocr_thr.stop) {
    struct win32overlapped *ov_set = NULL;

    EnterCriticalSection(head_cs);
    if (iocr_thr.ov_list.ov_head) {
      win32iocr_list_move(iocr_thr.ov_list, evq->iocr.ov_ready);
      SetEvent(head_signal);
    }
    ov_set = iocr_thr.ov_set;
    iocr_thr.ov_set = NULL;
    LeaveCriticalSection(head_cs);

    /* handle read/write requests */
    while (ov_set) {
      win32iocr_set_handle(&iocr_thr, ov_set);
      ov_set = ov_set->o.ov_next;
    }

    if (!iocr_thr.ov_list.ov_head)
      SleepEx(INFINITE, TRUE);
  }
  return 0;
}

static void
win32iocr_init (struct event_queue *evq)
{
  HANDLE hThr;
  DWORD id;

  if (g_IOCR_TLSIndex == TLS_OUT_OF_INDEXES) {
    g_IOCR_TLSIndex = TlsAlloc();
    if (g_IOCR_TLSIndex == TLS_OUT_OF_INDEXES)
      return;
  }

  hThr = CreateThread(NULL, 4096,
   (LPTHREAD_START_ROUTINE) win32iocr_wait, evq, 0, &id);
  if (hThr) {
    SetThreadPriority(hThr, THREAD_PRIORITY_ABOVE_NORMAL);
    WaitForSingleObject(evq->ack_event, INFINITE);
    evq->iocr.h = hThr;
  }
}

static void
win32iocr_done (struct event_queue *evq)
{
  struct win32overlapped **ovp = evq->ov_buffers;
  unsigned int i;

  if (evq->iocr.h) {
    evq->iocr.iocr_thr->stop = 1;
    if (!win32iocr_signal(evq))
      WaitForSingleObject(evq->iocr.h, INFINITE);
    CloseHandle(evq->iocr.h);
  }

  for (i = 0; *ovp && i < WIN32OV_BUF_SIZE; ++i) {
    free(*ovp++);
  }
}

static struct event *
win32iocr_process (struct event_queue *evq, struct win32overlapped *ov,
                   struct event *ev_ready, const msec_t now)
{
  struct win32overlapped *ov_next;

  for (; ov; ov = ov_next) {
    struct event *ev;
    const DWORD err = ov->il.err;
    const int cancelled = (err == STATUS_CANCELLED) || !ov->e.ev;

    ov_next = ov->o.ov_next;
    win32iocr_overlapped_del(evq, ov);
    if (cancelled)
      continue;

    ev = ov->e.ev;
    ev->w.ov = NULL;
    ev->flags &= ~EVENT_AIO_PENDING;  /* have to set AIO request */
    ev->flags |= err ? EVENT_EOF_RES
     : ((ev->flags & EVENT_READ) ? EVENT_READ_RES : EVENT_WRITE_RES);

    if (!(ev->flags & EVENT_ACTIVE)) {
      ev->flags |= EVENT_ACTIVE;
      if (ev->flags & EVENT_ONESHOT)
        evq_del(ev, 1);
      else if (ev->tq && !(ev->flags & EVENT_TIMEOUT_MANUAL))
        timeout_reset(ev, now);

      ev->next_ready = ev_ready;
      ev_ready = ev;
    }
  }
  return ev_ready;
}

static void
win32iocr_cancel (struct event_queue *evq, struct event *ev)
{
  struct win32overlapped *ov = ev->w.ov;

  ov->e.ev = NULL;
  ev->w.ov = NULL;
  ev->flags &= ~EVENT_AIO_PENDING;

  if (pCancelIoEx)
    pCancelIoEx(ev->fd, (OVERLAPPED *) ov);
  else
    win32iocr_apc_put(win32iocr_apc_cancel, evq, (ULONG_PTR) ev->fd);
}

static int
win32iocr_set (struct event *ev, const unsigned int rw_flags)
{
  struct event_queue *evq = ev->wth->evq;
  struct win32overlapped *ov = win32iocr_overlapped_new(evq);

  if (!ov) return -1;

  ov->ih.rw_flags = rw_flags & (EVENT_READ | EVENT_WRITE);
  ov->e.ev = ev;
  ev->w.ov = ov;
  ev->flags |= EVENT_AIO_PENDING;  /* AIO request is installed */
  win32iocr_list_put(evq->iocr.ov_list, ov);
  return 0;
}

