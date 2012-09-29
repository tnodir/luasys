#ifndef EVQ_TIMEOUT_H
#define EVQ_TIMEOUT_H

/*
 * Timer values are spread in small range (usually several minutes)
 * and overflow each 49.7 days.
 */

#define MIN_TIMEOUT	10  /* milliseconds */
#define MAX_TIMEOUT	(~0U >> 1)  /* milliseconds */

struct timeout_queue {
  struct timeout_queue *tq_prev, *tq_next;
  struct event *ev_head, *ev_tail;
  msec_t msec;
};

/* Helper function to fast lookup */
typedef struct timeout_queue *(*timeout_map_fn) (struct event_queue *evq,
                                                 struct timeout_queue *tq,
                                                 const msec_t msec,
                                                 const int is_remove);

#endif
