#ifndef POLL_H
#define POLL_H

#include <poll.h>

#define EVQ_SOURCE	"poll.c"

#define NEVENT		64

#define EVENT_EXTRA							\
    struct event_queue *evq;						\
    unsigned int index;

#define EVQ_EXTRA							\
    struct timeout_queue *tq;						\
    fd_t sig_fd[2];  /* pipe to notify about signals */			\
    unsigned int npolls, max_polls;					\
    struct event **events;						\
    struct pollfd *fdset;

#endif
