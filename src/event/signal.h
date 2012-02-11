#ifndef EVQ_SIGNAL_H
#define EVQ_SIGNAL_H

#ifdef _WIN32

#define EVQ_SIGINT	CTRL_C_EVENT
#define EVQ_SIGQUIT	CTRL_BREAK_EVENT
#define EVQ_SIGHUP	CTRL_LOGOFF_EVENT
#define EVQ_SIGTERM	CTRL_SHUTDOWN_EVENT
#define EVQ_NSIG 	4

#define EVQ_SIGEVQ	(EVQ_SIGTERM + 1)

#else

#define EVQ_SIGINT	SIGINT
#define EVQ_SIGQUIT	SIGQUIT
#define EVQ_SIGHUP	SIGHUP
#define EVQ_SIGTERM	SIGTERM
#define EVQ_SIGCHLD	SIGCHLD
#define EVQ_NSIG 	5

#define EVQ_SIGEVQ	SIGPIPE

typedef void (*sig_handler_t) (const int);

EVQ_API int signal_set (const int signo, sig_handler_t func);

#endif

EVQ_API void signal_init (void);

EVQ_API int evq_signal (struct event_queue *evq, const int signo);
EVQ_API int evq_ignore_signal (struct event_queue *evq, const int signo,
                               const int ignore);

#endif
