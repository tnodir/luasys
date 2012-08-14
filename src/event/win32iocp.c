/* Win32 NT IOCP */

#define NENTRY	64


static void
win32iocp_done (struct event_queue *evq)
{
    struct win32overlapped **ovp = evq->ov_buffers;
    unsigned int i;

    for (i = 0; *ovp && i < sizeof(evq->ov_buffers) / sizeof(void *); ++i) {
	free(*ovp++);
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

	if (pGetQueuedCompletionStatusEx) {
	    if (!nentries && !pGetQueuedCompletionStatusEx(iocph,
	     entries, NENTRY, &nentries, 0L, FALSE))
		break;

	    {
		const OVERLAPPED_ENTRY *ove = &entries[--nentries];
		const DWORD err = (DWORD) ove->lpOverlapped->Internal;

		ov = (struct win32overlapped *) ove->lpOverlapped;
		status = !err;
		cancelled = (err == STATUS_CANCELLED);
	    }
	} else {
	    ULONG_PTR key;
	    DWORD nr;

	    status = GetQueuedCompletionStatus(iocph, &nr, &key,
	     (OVERLAPPED **) &ov, 0L);
	    if (!status) {
		const DWORD err = GetLastError();

		if (err == WAIT_TIMEOUT)
		    break;
		cancelled = (err == ERROR_OPERATION_ABORTED);
	    }
	}

	if (!ov) {
	    if (pGetQueuedCompletionStatusEx) continue;
	    break;  /* error */
	}

	ev = ov->ev;
	cancelled = ev ? cancelled : 1;
	win32iocp_del_overlapped(evq, ov);
	if (cancelled)
	    continue;

	if (!status)
	    ev->flags |= EVENT_EOF_RES;
	else if (ov == ev->w.iocp.rov) {
	    ev->w.iocp.rov = NULL;
	    ev->flags |= EVENT_READ_RES;
	    ev->flags &= ~EVENT_RPENDING;  /* have to set IOCP read request */
	} else {
	    ev->w.iocp.wov = NULL;
	    ev->flags |= EVENT_WRITE_RES;
	    ev->flags &= ~EVENT_WPENDING;  /* have to set IOCP write request */
	}

	if (ev->flags & EVENT_ACTIVE)
	    continue;
	ev->flags |= EVENT_ACTIVE;
	if (ev->flags & EVENT_ONESHOT)
	    evq_del(ev, 1);
	else if (ev->tq && !(ev->flags & EVENT_TIMEOUT_MANUAL)) {
	    if (now == 0L) {
		now = evq->now = sys_milliseconds();
	    }
	    timeout_reset(ev, now);
	}

	ev->next_ready = ev_ready;
	ev_ready = ev;
    }
    return ev_ready;
}

static void
win32iocp_cancel (struct event *ev, unsigned int rw_flags)
{
    if (!pCancelIoEx) {
	CancelIo(ev->fd);
	rw_flags = (EVENT_READ | EVENT_WRITE);
    }
    if ((rw_flags & EVENT_READ) && ev->w.iocp.rov) {
	if (pCancelIoEx) pCancelIoEx(ev->fd, (OVERLAPPED *) ev->w.iocp.rov);
	ev->w.iocp.rov->ev = NULL;
	ev->w.iocp.rov = NULL;
	ev->flags &= ~EVENT_RPENDING;
    }
    if ((rw_flags & EVENT_WRITE) && ev->w.iocp.wov) {
	if (pCancelIoEx) pCancelIoEx(ev->fd, (OVERLAPPED *) ev->w.iocp.wov);
	ev->w.iocp.wov->ev = NULL;
	ev->w.iocp.wov = NULL;
	ev->flags &= ~EVENT_WPENDING;
    }
}

EVQ_API int
win32iocp_set (struct event *ev, const unsigned int rw_flags)
{
    static WSABUF buf = {0, 0};

    struct event_queue *evq = ev->wth->evq;
    const sd_t sd = (sd_t) ev->fd;

    if ((rw_flags & EVENT_READ) && !ev->w.iocp.rov) {
	struct win32overlapped *ov = win32iocp_new_overlapped(evq);
	DWORD flags = 0;

	if (!ov) return -1;
	if (!WSARecv(sd, &buf, 1, NULL, &flags, (OVERLAPPED *) ov, NULL)) {
	    if (ev->flags & EVENT_AIO_SKIP) {
		win32iocp_del_overlapped(evq, ov);
		ev->flags |= EVENT_READ_RES | EVENT_ACTIVE;
		goto ready;
	    }
	} else if (WSAGetLastError() != WSA_IO_PENDING) {
	    win32iocp_del_overlapped(evq, ov);
	    return -1;
	}
	ov->ev = ev;
	ev->w.iocp.rov = ov;
	ev->flags |= EVENT_RPENDING;  /* IOCP read request is installed */
    }
    if ((rw_flags & EVENT_WRITE) && !ev->w.iocp.wov) {
	struct win32overlapped *ov = win32iocp_new_overlapped(evq);

	if (!ov) return -1;
	if (!WSASend(sd, &buf, 1, NULL, 0, (OVERLAPPED *) ov, NULL)) {
	    if (ev->flags & EVENT_AIO_SKIP) {
		win32iocp_del_overlapped(evq, ov);
		ev->flags |= EVENT_WRITE_RES | EVENT_ACTIVE;
		goto ready;
	    }
	} else if (WSAGetLastError() != WSA_IO_PENDING) {
	    win32iocp_del_overlapped(evq, ov);
	    return -1;
	}
	ov->ev = ev;
	ev->w.iocp.wov = ov;
	ev->flags |= EVENT_WPENDING;  /* IOCP write request is installed */
    }
    return 0;
 ready:
    if (ev->flags & EVENT_ONESHOT)
	evq_del(ev, 1);
    else if (ev->tq && !(ev->flags & EVENT_TIMEOUT_MANUAL))
	timeout_reset(ev, evq->now);

    ev->next_ready = evq->ev_ready;
    evq->ev_ready = ev;
    return 0;
}

