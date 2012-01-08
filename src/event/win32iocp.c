/* Win32 NT IOCP */

#define NENTRY	64

#ifndef STATUS_CANCELLED
#define STATUS_CANCELLED	((DWORD) 0xC0000120L)
#endif

#ifndef FILE_SKIP_SET_EVENT_ON_HANDLE
typedef struct _OVERLAPPED_ENTRY {
    ULONG_PTR lpCompletionKey;
    LPOVERLAPPED lpOverlapped;
    ULONG_PTR Internal;
    DWORD dwNumberOfBytesTransferred;
} OVERLAPPED_ENTRY, *LPOVERLAPPED_ENTRY;
#endif

typedef BOOL (WINAPI *PGQCSEx) (HANDLE handle, LPOVERLAPPED_ENTRY entries,
                                ULONG count, PULONG n, DWORD timeout, BOOL alertable);
typedef BOOL (WINAPI *PCIOEx) (HANDLE handle, LPOVERLAPPED overlapped);

static PGQCSEx gqcsex;
static PCIOEx cancelioex;


static void
win32iocp_init (void)
{
    gqcsex = (PGQCSEx) GetProcAddress(GetModuleHandleA("kernel32.dll"),
                                      "GetQueuedCompletionStatusEx");
    cancelioex = (PCIOEx) GetProcAddress(GetModuleHandleA("kernel32.dll"),
                                         "CancelIoEx");
}

static void
win32iocp_done (struct event_queue *evq)
{
    struct win32overlapped **ovp = evq->ov_buffers;
    int i;

    for (i = 0; *ovp && i < sizeof(evq->ov_buffers); ++i) {
	free(*ovp);
    }
}

static struct win32overlapped *
win32iocp_new_overlapped (struct event_queue *evq)
{
    struct win32overlapped *ov = evq->ov_free;

    if (ov) {
	evq->ov_free = ov->u.next_free;
    } else {
	const int n = evq->ov_buf_nevents;
	const int buf_idx = evq->ov_buf_index + EVQ_BUF_IDX;
	const int nmax = (1 << buf_idx);

	ov = evq->ov_buffers[buf_idx];
	if (ov) {
	    ov += n;
	    if (++evq->ov_buf_nevents >= nmax) {
		evq->ov_buf_nevents = 0;
		evq->ov_buf_index++;
	    }
	} else {
	    if (buf_idx > EVQ_BUF_MAX
	     || !(ov = malloc(nmax * sizeof(struct win32overlapped))))
		return NULL;
	    evq->ov_buffers[buf_idx] = ov;
	    evq->ov_buf_nevents = 1;
	}
    }
    memset(ov, 0, sizeof(struct win32overlapped));
    ov->u.ov.hEvent = evq->head.signal;
    return ov;
}

static void
win32iocp_del_overlapped (struct event_queue *evq, struct win32overlapped *ov)
{
    ov->u.next_free = evq->ov_free;
    evq->ov_free = ov;
}

static struct event *
win32iocp_process (struct event_queue *evq, struct event *ev_ready, msec_t now)
{
    const HANDLE iocph = evq->iocp.h;
    OVERLAPPED_ENTRY entries[NENTRY];
    ULONG nentries = 0;

    for (; ; ) {
	struct win32overlapped *ov;
	struct event *ev;
	BOOL status;
	int cancelled = 0;

	if (gqcsex) {
	    if (!nentries && !gqcsex(iocph, entries, NENTRY, &nentries, 0L, FALSE))
		break;

	    {
		const OVERLAPPED_ENTRY *ove = &entries[--nentries];
		const DWORD err = (DWORD) ove->lpOverlapped->Internal;

		ov = (struct win32overlapped *) ove->lpOverlapped;
		ev = (struct event *) ove->lpCompletionKey;
		status = !err;
		cancelled = (err == STATUS_CANCELLED);
	    }
	} else {
	    DWORD nr;

	    status = GetQueuedCompletionStatus(iocph, &nr, (ULONG_PTR *) &ev, (OVERLAPPED **) &ov, 0L);
	    if (!status) {
		const DWORD err = GetLastError();

		if (err == WAIT_TIMEOUT)
		    break;
		cancelled = (err == ERROR_OPERATION_ABORTED);
	    }
	}

	if (!ov || !ev) {
	    if (gqcsex) continue;
	    break;  /* error */
	}

	cancelled = ov->u.ov.hEvent ? cancelled : 1;
	win32iocp_del_overlapped(evq, ov);
	if (cancelled)
	    continue;

	if (!status)
	    ev->flags |= EVENT_EOF_RES;
	else if (ov == ev->rov) {
	    ev->rov = NULL;
	    ev->flags |= EVENT_READ_RES;
	} else {
	    ev->wov = NULL;
	    ev->flags |= EVENT_WRITE_RES;
	}

	if (ev->flags & EVENT_ACTIVE)
	    continue;
	ev->flags |= EVENT_ACTIVE;
	ev->flags &= ~EVENT_PENDING;  /* have to install IOCP request */
	if (ev->flags & EVENT_ONESHOT)
	    evq_del(ev, 1);
	else if (ev->tq && !(ev->flags & EVENT_TIMEOUT_MANUAL)) {
	    if (now == 0L) {
		now = evq->now = get_milliseconds();
	    }
	    timeout_reset(ev, now);
	}

	ev->next_ready = ev_ready;
	ev_ready = ev;
    }
    return ev_ready;
}

static void
win32iocp_cancel (struct event *ev, unsigned int ev_flags)
{
    ev->flags &= ~EVENT_PENDING;

    if (!cancelioex)
	CancelIo(ev->fd);
    else
	ev_flags = (EVENT_READ | EVENT_WRITE);

    if ((ev_flags & EVENT_READ) && ev->rov) {
	if (cancelioex) cancelioex(ev->fd, (OVERLAPPED *) ev->rov);
	ev->rov->u.ov.hEvent = NULL;
	ev->rov = NULL;
    }
    if ((ev_flags & EVENT_WRITE) && ev->wov) {
	if (cancelioex) cancelioex(ev->fd, (OVERLAPPED *) ev->wov);
	ev->wov->u.ov.hEvent = NULL;
	ev->wov = NULL;
    }
}

int
win32iocp_set (struct event *ev, unsigned int ev_flags)
{
    static WSABUF buf = {0, 0};

    struct event_queue *evq = ev->wth->evq;

    if ((ev_flags & EVENT_READ) && !ev->rov) {
	struct win32overlapped *ov = win32iocp_new_overlapped(evq);
	DWORD flags = 0;

	if (!ov) return -1;
	if (WSARecv((sd_t) ev->fd, &buf, 1, NULL, &flags, (OVERLAPPED *) ov, NULL)
	 && WSAGetLastError() != WSA_IO_PENDING) {
	    win32iocp_del_overlapped(evq, ov);
	    return -1;
	}
	ev->rov = ov;
	ev->flags |= EVENT_PENDING;  /* IOCP request is installed */
    }
    if ((ev_flags & EVENT_WRITE) && !ev->wov) {
	struct win32overlapped *ov = win32iocp_new_overlapped(evq);

	if (!ov) return -1;
	if (WSASend((sd_t) ev->fd, &buf, 1, NULL, 0, (OVERLAPPED *) ov, NULL)
	 && WSAGetLastError() != WSA_IO_PENDING) {
	    win32iocp_del_overlapped(evq, ov);
	    return -1;
	}
	ev->wov = ov;
	ev->flags |= EVENT_PENDING;  /* IOCP request is installed */
    }
    return 0;
}

