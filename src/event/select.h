#ifndef SELECT_H
#define SELECT_H

#define EVQ_SOURCE	"select.c"

#define EVENT_EXTRA							\
    struct event_queue *evq;						\
    unsigned int index;

#define EVQ_EXTRA							\
    struct timeout_queue *tq;						\
    fd_t sig_fd[2];  /* pipe to notify about signals */			\
    unsigned int npolls, max_fd;					\
    struct event *events[FD_SETSIZE];					\
    fd_set readset, writeset;

#endif
