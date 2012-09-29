#ifndef EVQ_EPOLL_H
#define EVQ_EPOLL_H

#include <sys/epoll.h>
#include <sys/inotify.h>

#ifdef USE_EVENTFD
#include <sys/eventfd.h>
#define NSIG_FD		1
#else
#define NSIG_FD		2
#endif

#define EVQ_SOURCE	"epoll.c"

#define NEVENT		64

#define EVENT_EXTRA							\
  struct event_queue *evq;

#define EVQ_EXTRA							\
  struct timeout_queue *tq;						\
  pthread_mutex_t sig_cs;						\
  int volatile sig_ready;  /* triggered signals */			\
  fd_t sig_fd[NSIG_FD];  /* eventfd or pipe to interrupt the loop */	\
  int epoll_fd;  /* epoll descriptor */

#endif
