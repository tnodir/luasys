#ifndef EVQ_H
#define EVQ_H

#ifndef EVQ_API
#define EVQ_API		static
#endif

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

#include "signal.h"
#include "timeout.h"

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
#define EVENT_REGWATCH		0x00000400  /* Windows registry watcher */
#define EVENT_TIMEOUT_MANUAL	0x00000800  /* don't auto-reset timeout on event */
#define EVENT_AIO		0x00001000
#define EVENT_SOCKET_ACC_CONN	0x00002000  /* socket is listening or connecting */
#define EVENT_CALLBACK		0x00010000  /* callback exists */
#define EVENT_CALLBACK_CORO	0x00020000  /* callback is coroutine */
#define EVENT_CALLBACK_SCHED	0x00040000  /* callback is scheduler */
/* triggered events (result of waiting) */
#define EVENT_ACTIVE		0x00080000
#define EVENT_READ_RES		0x00100000
#define EVENT_WRITE_RES		0x00200000
#define EVENT_TIMEOUT_RES	0x00400000
#define EVENT_EOF_RES		0x00800000
#define EVENT_MASK_RES		0x00F80000
/* options: directory/registry watcher */
#define EVENT_WATCH_MODIFY	0x01000000  /* watch only content changes */
#define EVENT_WATCH_RECURSIVE	0x02000000  /* watch directories recursively */
/* options: AIO requests */
#define EVENT_AIO_PENDING	0x01000000  /* AIO read/write request not completed */
/* options: process status (result of oneshot waiting) */
#define EVENT_STATUS_MASK	0xFF000000
#define EVENT_STATUS_SHIFT	24  /* last byte is process status */
  unsigned int flags;

  int ev_id;
  fd_t fd;

  EVENT_EXTRA
};

struct event_queue {
#define EVQ_FLAG_STOP		0x01  /* break the loop? */
#define EVQ_FLAG_WAITING	0x02  /* waiting events? */
  unsigned int volatile flags;

  unsigned int nevents;  /* number of alive events */

  int buf_nevents;  /* number of used events of current buffer */
  int buf_index;  /* environ. index of current buffer */

  msec_t now; /* current cached time */

  struct event * volatile ev_ready;  /* head of ready events */
  struct event *ev_free;  /* head of free events */

  timeout_map_fn tq_map_fn;

  EVQ_APP_EXTRA
  EVQ_EXTRA
};

EVQ_API int evq_init (struct event_queue *evq);
EVQ_API void evq_done (struct event_queue *evq);

EVQ_API int evq_add (struct event_queue *evq, struct event *ev);
EVQ_API int evq_add_dirwatch (struct event_queue *evq, struct event *ev,
                              const char *path);
EVQ_API int evq_del (struct event *ev, const int reuse_fd);

EVQ_API int evq_modify (struct event *ev, unsigned int flags);

EVQ_API int evq_wait (struct event_queue *evq, struct sys_thread *td,
                      msec_t timeout);

EVQ_API int evq_set_timeout (struct event *ev, const msec_t msec);
EVQ_API int evq_add_timer (struct event_queue *evq, struct event *ev,
                           const msec_t msec);

EVQ_API struct event *evq_process_active (struct event *ev,
                                          struct event *ev_ready,
                                          const msec_t now);

#ifndef _WIN32

#define event_get_evq(ev)	(ev)->evq
#define event_get_tq_head(ev)	(ev)->evq->tq
#define event_deleted(ev)	((ev)->evq == NULL)
#define evq_is_empty(evq)	(!(evq)->nevents)

#endif

#endif
