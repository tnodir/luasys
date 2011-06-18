#ifndef TIMEOUT_H
#define TIMEOUT_H

/*
 * Timer values are spread in small range (usually several minutes)
 * and overflow each 49.7 days.
 */

#define MIN_TIMEOUT	10  /* milliseconds */

struct timeout_queue {
    struct timeout_queue *tq_prev, *tq_next;
    struct event *ev_head, *ev_tail;
    msec_t msec;
};

#endif
