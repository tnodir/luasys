#ifndef EVQ_POLL_H
#define EVQ_POLL_H

#include <poll.h>

#define EVQ_SOURCE	"poll.c"

#define NEVENT		64

#define EVENT_EXTRA							\
  struct event_queue *evq;						\
  unsigned int index;

#define EVQ_EXTRA							\
  struct timeout_queue *tq;						\
  pthread_mutex_t sig_cs;						\
  int volatile sig_ready;  /* triggered signals */			\
  fd_t sig_fd[2];  /* pipe to interrupt the loop */			\
  unsigned int npolls, max_polls;					\
  struct event **events;						\
  struct pollfd *fdset;

#endif
