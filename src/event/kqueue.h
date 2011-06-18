#ifndef KQUEUE_H
#define KQUEUE_H

#include <sys/event.h>

#define EVQ_SOURCE	"kqueue.c"

#define NEVENT		64

#define EVENT_EXTRA							\
    struct event_queue *evq;

#define EVQ_EXTRA							\
    struct timeout_queue *tq;						\
    int kqueue_fd;  /* kqueue descriptor */				\
    unsigned int nchanges;						\
    struct kevent kev_list[NEVENT];

#endif
