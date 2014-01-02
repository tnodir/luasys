#ifndef EVQ_WIN32_H
#define EVQ_WIN32_H

#define EVQ_SOURCE	"win32.c"

#define NEVENT		(MAXIMUM_WAIT_OBJECTS-1)

/* Win32 Thread */
struct win32thr {
  struct event_queue *evq;
  struct timeout_queue *tq;
  struct win32thr *next, *next_ready;

  CRITICAL_SECTION sig_cs;
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

struct win32overlapped {
  union {
    DWORD Internal;
    DWORD err;
  } il;
  union {
    DWORD InternalHigh;
    DWORD rw_flags;
  } ih;
  union {
    struct {
      DWORD Offset;
      DWORD OffsetHigh;
    } w;
    struct win32overlapped *ov_next;
  } o;
  union {
    HANDLE hEvent;
    struct event *ev;
  } e;
};

/* List of overlaps */
struct win32overlapped_list {
  struct win32overlapped *ov_head, *ov_tail;
};

/* IOCR thread */
struct win32iocr_thread {
  int stop;  /* terminate the thread? */
  struct win32overlapped_list ov_list;  /* overlaps to be ready */
  struct win32overlapped * volatile ov_set;  /* set overlaps to queue */
};

/* Win32 NT I/O Completion Routines */
struct win32iocr {
  HANDLE h;  /* thread handle */
  struct win32overlapped_list ov_list;  /* overlaps to be queued */
  struct win32iocr_thread *iocr_thr;  /* IOCR thread */
  struct win32overlapped * volatile ov_ready;  /* ready overlaps */
};

#define EVENT_EXTRA							\
  struct win32thr *wth;							\
  union {								\
    unsigned int index;							\
    struct win32overlapped *ov;  /* IOCR overlap */			\
  } w;

#define WIN32OV_BUF_IDX		6  /* initial buffer size on power of 2 */
#define WIN32OV_BUF_MAX		24  /* maximum buffer size on power of 2 */
#define WIN32OV_BUF_SIZE	(WIN32OV_BUF_MAX - WIN32OV_BUF_IDX + 1)

#define EVQ_EXTRA							\
  HANDLE ack_event;							\
  struct event *win_msg;  /* window messages handler */			\
  struct win32thr * volatile wth_ready;					\
  int volatile nwakeup;  /* number of the re-polling threads */		\
  int volatile sig_ready;  /* triggered signals */			\
  struct win32thr head;							\
  struct win32iocr iocr;						\
  int ov_buf_nevents;  /* number of used overlaps of cur. buffer */	\
  int ov_buf_index;  /* index of current buffer */			\
  struct win32overlapped *ov_free;  /* head of free overlaps */		\
  struct win32overlapped *ov_buffers[WIN32OV_BUF_SIZE];

EVQ_API int evq_add_regwatch (struct event_queue *evq, struct event *ev,
                              HKEY hk);

#define event_get_evq(ev)	(ev)->wth->evq
#define event_get_tq_head(ev)	(ev)->wth->tq
#define event_deleted(ev)	((ev)->wth == NULL)
#define evq_is_empty(evq)	(!((evq)->nevents || (evq)->head.next))

/* Have to initialize the event source */
#define EVQ_POST_INIT

EVQ_API int evq_post_init (struct event *ev);

#endif
