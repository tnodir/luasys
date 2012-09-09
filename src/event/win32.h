#ifndef EVQ_WIN32_H
#define EVQ_WIN32_H

#define EVQ_SOURCE	"win32.c"

#define NEVENT		(MAXIMUM_WAIT_OBJECTS-1)

/* Win32 Thread */
struct win32thr {
  struct event_queue *evq;
  struct timeout_queue *tq;
  struct win32thr *next, *next_ready;

  CRITICAL_SECTION cs;
  HANDLE signal;
  unsigned int volatile n;  /* count of events */

#define WTHR_SLEEP	1
#define WTHR_POLL	2
#define WTHR_READY	3
#define WTHR_ACK	4
  unsigned int volatile state;

  unsigned int idx;  /* result of Wait* */
  HANDLE handles[NEVENT];  /* last handle is reserved for signal event */
  struct event *events[NEVENT-1];
};

/* Win32 IOCP */
struct win32iocp {
  int n;  /* number of assosiated events */
  HANDLE h;  /* IOCP handle */
};

struct win32overlapped {
  union {
    struct win32overlapped *next_free;
    OVERLAPPED ov;
  } u;
  struct event *ev;
};

#define EVENT_EXTRA							\
  struct win32thr *wth;							\
  union {								\
    unsigned int index;							\
    struct {								\
      struct win32overlapped *rov, *wov;  /* IOCP overlaps */		\
    } iocp;								\
  } w;

#define WIN32OV_BUF_IDX		6  /* initial buffer size on power of 2 */
#define WIN32OV_BUF_MAX		24  /* maximum buffer size on power of 2 */
#define WIN32OV_BUF_SIZE	(WIN32OV_BUF_MAX - WIN32OV_BUF_IDX + 1)

#define EVQ_EXTRA							\
  HANDLE ack_event;							\
  struct event *win_msg;  /* window messages handler */			\
  struct win32iocp iocp;						\
  struct win32thr * volatile wth_ready;					\
  struct win32thr head;							\
  int volatile nwakeup;  /* number of the re-polling threads */		\
  int volatile sig_ready;  /* triggered signals */			\
  int ov_buf_nevents;  /* number of used overlaps of cur. buffer */	\
  int ov_buf_index;  /* index of current buffer */			\
  struct win32overlapped *ov_free;  /* head of free overlaps */		\
  struct win32overlapped *ov_buffers[WIN32OV_BUF_SIZE];

#define event_get_evq(ev)	(ev)->wth->evq
#define event_get_tq_head(ev)	(ev)->wth->tq
#define event_deleted(ev)	((ev)->wth == NULL)
#define iocp_is_empty(evq)	(!(evq)->iocp.n)
#define evq_is_empty(evq)	(!((evq)->nevents || (evq)->head.next))

#define EVQ_POST_INIT

#define evq_post_init(ev)						\
  do {									\
    if (((ev)->flags & (EVENT_AIO | EVENT_PENDING | EVENT_ACTIVE)) == EVENT_AIO) \
      win32iocp_set((ev), (ev)->flags);					\
    else if ((ev)->flags & EVENT_DIRWATCH)				\
      FindNextChangeNotification((ev)->fd);				\
  } while (0)

EVQ_API int win32iocp_set (struct event *ev, const unsigned int ev_flags);

#endif
