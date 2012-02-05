/* Lua System: Threading: Synchronization */

/* Critical Section */
#ifndef _WIN32
typedef pthread_mutex_t		thread_critsect_t;
#else
typedef CRITICAL_SECTION	thread_critsect_t;
#endif


static int
thread_critsect_new (thread_critsect_t *tcs)
{
#ifndef _WIN32
    const int res = pthread_mutex_init(tcs, NULL);
    if (res) errno = res;
    return res;
#else
    return !InitCriticalSection(tcs);
#endif
}

#ifndef _WIN32
#define thread_critsect_del(tcs)	pthread_mutex_destroy(tcs)
#else
#define thread_critsect_del(tcs)	DeleteCriticalSection(tcs)
#endif

#ifndef _WIN32
#define thread_critsect_enter(tcs)	pthread_mutex_lock(tcs)
#else
#define thread_critsect_enter(tcs)	EnterCriticalSection(tcs)
#endif

#ifndef _WIN32
#define thread_critsect_leave(tcs)	pthread_mutex_unlock(tcs)
#else
#define thread_critsect_leave(tcs)	LeaveCriticalSection(tcs)
#endif


/* Condition */
#ifndef _WIN32
typedef pthread_cond_t		thread_cond_t;
#else
typedef HANDLE			thread_cond_t;
#endif


static int
thread_cond_new (thread_cond_t *tcond)
{
#ifndef _WIN32
    const int res = pthread_cond_init(tcond, NULL);
    if (res) errno = res;
    return res;
#else
    *tcond = CreateEvent(NULL, FALSE, FALSE, NULL);  /* auto-reset */
    return (*tcond != NULL) ? 0 : -1;
#endif
}

#ifndef _WIN32
#define thread_cond_del(tcond)		pthread_cond_destroy(tcond)
#else
#define thread_cond_del(tcond)		CloseHandle(*tcond)
#endif

#ifndef _WIN32
#define thread_cond_signal(tcond)	pthread_cond_signal(tcond)
#else
#define thread_cond_signal(tcond)	!SetEvent(*tcond)
#endif


/* Event */
typedef struct {
    thread_cond_t cond;
#ifndef _WIN32
    thread_critsect_t cs;
#define THREAD_COND_SIGNALLED	1
    unsigned int volatile signalled;
#endif
} thread_event_t;


static int
thread_event_new (thread_event_t *tev)
{
    int res = thread_cond_new(&tev->cond);
#ifndef _WIN32
    if (!res) {
	res = thread_critsect_new(&tev->cs);
	if (res) thread_cond_del(&tev->cond);
    }
#endif
    return res;
}

static int
thread_event_del (thread_event_t *tev)
{
#ifndef _WIN32
    thread_critsect_del(&tev->cs);
#endif
    return thread_cond_del(&tev->cond);
}

#ifndef _WIN32
static int
thread_event_wait_impl (pthread_cond_t *condp, pthread_mutex_t *csp,
                        volatile unsigned int *signalled,
                        const unsigned int test_value,
                        const int reset, const msec_t timeout)
{
    int res = 0;

    if (timeout == TIMEOUT_INFINITE) {
	pthread_mutex_lock(csp);
	while (*signalled != test_value && !res)
	    res = pthread_cond_wait(condp, csp);
    } else {
	struct timespec ts;
	struct timeval tv;

	gettimeofday(&tv, NULL);
	tv.tv_sec += timeout / 1000;
	tv.tv_usec += (timeout % 1000) * 1000;
	if (tv.tv_usec >= 1000000) {
	    tv.tv_sec++;
	    tv.tv_usec -= 1000000;
	}

	ts.tv_sec = tv.tv_sec;
	ts.tv_nsec = tv.tv_usec * 1000;

	pthread_mutex_lock(csp);
	while (*signalled != test_value && !res)
	    res = pthread_cond_timedwait(condp, csp, &ts);
    }
    if (!res && reset) *signalled = 0;
    pthread_mutex_unlock(csp);

    if (res) {
	if (res == ETIMEDOUT)
	    return 1;
	errno = res;
	return -1;
    }
    return 0;
}
#else
static int
thread_event_wait_impl (HANDLE h, msec_t timeout)
{
    const int res = WaitForSingleObject(h, timeout);

    return (res == WAIT_OBJECT_0) ? 0
     : (res == WAIT_TIMEOUT) ? 1 : -1;
}
#endif

static int
thread_event_wait (thread_event_t *tev, msec_t timeout)
{
    int res;

    sys_vm_leave();
#ifndef _WIN32
    res = thread_event_wait_impl(&tev->cond, &tev->cs,
     &tev->signalled, THREAD_COND_SIGNALLED, 1, timeout);
#else
    res = thread_event_wait_impl(tev->cond, timeout);
#endif
    sys_vm_enter();
    return res;
}

#ifndef _WIN32
#define thread_event_signal_nolock(tev) \
	((tev)->signalled = THREAD_COND_SIGNALLED, pthread_cond_signal(&(tev)->cond))
#else
#define thread_event_signal_nolock(tev)		!SetEvent((tev)->cond)
#endif

static int
thread_event_signal (thread_event_t *tev)
{
#ifndef _WIN32
    pthread_mutex_t *csp = &tev->cs;
    int res;

    pthread_mutex_lock(csp);
    tev->signalled = THREAD_COND_SIGNALLED;
    res = pthread_cond_signal(&tev->cond);
    pthread_mutex_unlock(csp);

    if (res) errno = res;
    return res;
#else
    return !SetEvent(tev->cond);
#endif
}

