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

static PGQCSEx gqcsex;


static void
win32iocp_init (void)
{
    gqcsex = (PGQCSEx) GetProcAddress(GetModuleHandleA("kernel32.dll"),
                                      "GetQueuedCompletionStatusEx");
}

static struct event *
win32iocp_process (struct event_queue *evq, struct event *ev_ready, msec_t now)
{
    const HANDLE iocph = evq->iocp.h;
    const OVERLAPPED *wov = &evq->wov;
    OVERLAPPED_ENTRY entries[NENTRY];
    ULONG nentries = 0;

    for (; ; ) {
	OVERLAPPED *ovp;
	struct event *ev;
	BOOL status;

	if (gqcsex) {
	    if (!nentries && !gqcsex(iocph, entries, NENTRY, &nentries, 0L, FALSE))
		break;

	    {
		const OVERLAPPED_ENTRY *ovep = &entries[--nentries];
		const DWORD err = (DWORD) ovep->lpOverlapped->Internal;

		if (err == STATUS_CANCELLED)
		    continue;
		status = !err || err == STATUS_PENDING;
		ovp = ovep->lpOverlapped;
		ev = (struct event *) ovep->lpCompletionKey;
	    }
	} else {
	    DWORD nr;

	    status = GetQueuedCompletionStatus(iocph, &nr, (ULONG_PTR *) &ev, &ovp, 0L);
	    if (!status) {
		const DWORD err = GetLastError();

		if (err == WAIT_TIMEOUT)
		    break;
		if (err == ERROR_OPERATION_ABORTED)
		    continue;
	    }
	}

	if (status) {
	    ev->flags |= (ovp == wov) ? EVENT_WRITE_RES : EVENT_READ_RES;
	} else {
	    if (ovp && ev)
		ev->flags |= EVENT_EOF_RES;
	    else {
		if (gqcsex) continue;
		break;  /* error */
	    }
	}

	ev->flags &= ~EVENT_PENDING;  /* have to install IOCP request */
	ev->flags |= EVENT_ACTIVE;
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

int
win32iocp_set (struct event *ev, unsigned int ev_flags)
{
    struct event_queue *evq = ev->wth->evq;
    WSABUF buf = {0, 0};

    if (ev_flags & EVENT_READ) {
	DWORD flags = 0;

	if (WSARecv((sd_t) ev->fd, &buf, 1, NULL, &flags, &evq->rov, NULL)
	 && WSAGetLastError() != WSA_IO_PENDING)
	    return -1;
    }
    if (ev_flags & EVENT_WRITE) {
	if (WSASend((sd_t) ev->fd, &buf, 1, NULL, 0, &evq->wov, NULL)
	 && WSAGetLastError() != WSA_IO_PENDING)
	    return -1;
    }
    ev->flags |= EVENT_PENDING;  /* IOCP request is installed */
    return 0;
}

