#ifndef EVQ_KQUEUE_H
#define EVQ_KQUEUE_H

#include <sys/event.h>

#define EVQ_SOURCE	"kqueue.c"

#define NEVENT		64

#define EVENT_EXTRA							\
  struct event_queue *evq;

#define EVQ_EXTRA							\
  struct timeout_queue *tq;						\
  pthread_mutex_t sig_cs;						\
  int volatile sig_ready;  /* triggered signals */			\
  fd_t sig_fd[2];  /* pipe to interrupt the loop */			\
  int kqueue_fd;  /* kqueue descriptor */				\
  unsigned int nchanges;						\
  struct kevent kev_list[NEVENT];

#if defined (__NetBSD__)
typedef intptr_t	kev_udata_t;
#else
typedef void *		kev_udata_t;
#endif

#endif
