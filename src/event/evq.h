#ifndef EVQ_H
#define EVQ_H

struct event;
struct event_queue;

#if defined(_WIN32)
#include "win32.h"
#elif defined(USE_KQUEUE)
#include "kqueue.h"
#elif defined(USE_EPOLL)
#include "epoll.h"
#elif defined(USE_POLL)
#include "poll.h"
#else
#include "select.h"
#endif

#include "timeout.h"

/* Event Queue wait result */
#define EVQ_TIMEOUT	1
#define EVQ_FAILED	-1

/* Event Queue environ. table reserved indexes */
#define EVQ_OBJ_UDATA	1  /* table: event objects */
#define EVQ_CALLBACK	2  /* table: callback functions */
#define EVQ_ON_INTR	3  /* function */
#define EVQ_BUF_IDX	6  /* initial buffer index */

/* Directory watcher filter flags */
#define EVQ_DIRWATCH_MODIFY	0x01

struct event {
    struct event *next_ready, *next_object;

    /* timeout */
    struct event *prev, *next;
    struct timeout_queue *tq;
    msec_t timeout_at;

#define EVENT_READ		0x00000001
#define EVENT_WRITE		0x00000002
#define EVENT_ONESHOT		0x00000004
#define EVENT_DELETE		0x00000008
#define EVENT_SOCKET		0x00000010
#define EVENT_TIMER		0x00000020
#define EVENT_PID		0x00000040
#define EVENT_SIGNAL		0x00000080
#define EVENT_WINMSG		0x00000100
#define EVENT_DIRWATCH		0x00000200  /* directory watcher */
#define EVENT_OBJECT		0x00000400  /* triggerable object */
#define EVENT_AIO		0x00000800
#define EVENT_CALLBACK		0x00001000  /* callback exist */
#define EVENT_CALLBACK_THREAD	0x00002000  /* callback is coroutine */
#define EVENT_SOCKET_ACC_CONN	0x00004000  /* IOCP: don't use listening or connecting socket */
#define EVENT_PENDING		0x00008000  /* AIO request not completed */
#define EVENT_MASK		0x0000FFFF
/* triggered events (result of waiting) */
#define EVENT_ACTIVE		0x00010000
#define EVENT_READ_RES		0x00100000
#define EVENT_WRITE_RES		0x00200000
#define EVENT_TIMEOUT_RES	0x00400000
#define EVENT_EOF_RES		0x01000000
#define EVENT_EOF_MASK_RES	0xFF000000
#define EVENT_EOF_SHIFT_RES	24  /* last byte is error status */
    unsigned int flags;

    int ev_id;
    fd_t fd;

    EVENT_EXTRA
};

struct event_queue {
    unsigned int stop:		1;  /* break the loop? */
    unsigned int intr:		1;  /* is interrupted? */

    unsigned int nevents;  /* number of alive events */

    int buf_nevents;  /* number of used events of current buffer */
    int buf_index;  /* environ. index of current buffer */

    msec_t now; /* current cached time */

    struct event * volatile ev_ready;  /* head of ready events */
    struct event *ev_free;  /* head of free events */

    struct sys_thread *vmtd;  /* for inter-vm events (eg. threads i/o) */

    EVQ_EXTRA
};

int evq_init (struct event_queue *evq);
void evq_done (struct event_queue *evq);

int evq_add (struct event_queue *evq, struct event *ev);
int evq_add_dirwatch (struct event_queue *evq, struct event *ev, const char *path);
int evq_del (struct event *ev, int reuse_fd);

int evq_modify (struct event *ev, unsigned int flags);

int evq_wait (struct event_queue *evq, msec_t timeout);

int evq_set_timeout (struct event *ev, msec_t msec);
int evq_add_timer (struct event_queue *evq, struct event *ev, msec_t msec);

int evq_ignore_signal (struct event_queue *evq, int signo, int ignore);
int evq_interrupt (struct event_queue *evq);


#ifndef _WIN32

#define evq_post_call(ev, ev_flags)	((void) 0)

#define event_get_evq(ev)	(ev)->evq
#define event_get_tq_head(ev)	(ev)->evq->tq
#define event_deleted(ev)	((ev)->evq == NULL)
#define evq_is_empty(evq)	(!(evq)->nevents)

typedef void (*sig_handler_t) (int);

int signal_set (int signo, sig_handler_t func);

#endif

#endif
