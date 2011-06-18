/* Win32 NT IOCP */

static struct event *
win32iocp_process (struct event_queue *evq, struct event *ev_ready, msec_t now)
{
    const HANDLE iocph = evq->iocp.h;
    const OVERLAPPED *wov = &evq->wov;

    for (; ; ) {
	unsigned long nr;
	struct event *ev;
	OVERLAPPED *ovp;

	if (GetQueuedCompletionStatus(iocph, &nr, (ULONG_PTR *) &ev, &ovp, 0L)) {
	    ev->flags |= (ovp == wov) ? EVENT_WRITE_RES : EVENT_READ_RES;
	} else {
	    const int err = GetLastError();

	    if (err == WAIT_TIMEOUT)
		break;
	    if (err == ERROR_OPERATION_ABORTED)
		continue;
	    if (ovp && ev)
		ev->flags |= EVENT_EOF_RES;
	    else
		break;  /* error */
	}

	ev->flags &= ~EVENT_PENDING;  /* have to install IOCP request */
	ev->flags |= EVENT_ACTIVE;
	if (ev->flags & EVENT_ONESHOT)
	    evq_del(ev, 1);
	else if (ev->tq) {
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

int
win32iocp_set (struct event *ev, unsigned int ev_flags)
{
    struct event_queue *evq = ev->wth->evq;
    WSABUF buf = {0};

    if (ev_flags & EVENT_READ) {
	DWORD flags = 0;

	if (WSARecv((sd_t) ev->fd, &buf, 1, NULL, &flags, &evq->rov, NULL) != SOCKET_ERROR
	 || WSAGetLastError() != WSA_IO_PENDING)
	    return -1;
    }
    if (ev_flags & EVENT_WRITE) {
	if (WSASend((sd_t) ev->fd, &buf, 1, NULL, 0, &evq->wov, NULL) != SOCKET_ERROR
	 || WSAGetLastError() != WSA_IO_PENDING)
	    return -1;
    }
    ev->flags |= EVENT_PENDING;  /* IOCP request is installed */
    return 0;
}

