#ifndef EPOLL_H
#define EPOLL_H

#include <sys/epoll.h>
#include <sys/inotify.h>

#define EVQ_SOURCE	"epoll.c"

#define NEVENT		64

#define EVENT_EXTRA							\
    struct event_queue *evq;

#define EVQ_EXTRA							\
    struct timeout_queue *tq;						\
    fd_t sig_fd[2];  /* pipe to notify about signals */			\
    int epoll_fd;  /* epoll descriptor */

#endif
