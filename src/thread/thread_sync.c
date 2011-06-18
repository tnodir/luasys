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


/* Event */
typedef struct {
#ifndef _WIN32
    pthread_cond_t cond;
    thread_critsect_t cs;
#else
    HANDLE h;
#endif
} thread_event_t;


static int
thread_event_new (thread_event_t *tev)
{
#ifndef _WIN32
    int res;

    res = pthread_cond_init(&tev->cond, NULL);
    if (!res) {
	res = pthread_mutex_init(&tev->cs, NULL);
	if (res)
	    pthread_cond_destroy(&tev->cond);
    }
    if (res) errno = res;
    return res;
#else
    tev->h = CreateEvent(NULL, FALSE, FALSE, NULL);  /* auto-reset */
    return (tev->h != NULL) ? 0 : -1;
#endif
}

static int
thread_event_del (thread_event_t *tev)
{
    int res;

#ifndef _WIN32
    res = pthread_cond_destroy(&tev->cond);
    if (!res)
	res = pthread_mutex_destroy(&tev->cs);
    if (res) errno = res;
#else
    res = 0;
    if (tev->h) {
	res = !CloseHandle(tev->h);
	tev->h = NULL;
    }
#endif
    return res;
}

static int
thread_event_wait (thread_event_t *tev, msec_t timeout)
{
    int res;

#ifndef _WIN32
    pthread_mutex_t *csp = &tev->cs;

    sys_vm_leave();
    if (timeout == TIMEOUT_INFINITE) {
	pthread_mutex_lock(csp);
	res = pthread_cond_wait(&tev->cond, &tev->cs);
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
	res = pthread_cond_timedwait(&tev->cond, &tev->cs, &ts);
    }
    pthread_mutex_unlock(csp);
    sys_vm_enter();

    if (res) {
	if (res == ETIMEDOUT)
	    return 1;
	errno = res;
	return -1;
    }
    return 0;
#else
    sys_vm_leave();
    res = WaitForSingleObject(tev->h, timeout);
    sys_vm_enter();

    return (res == WAIT_OBJECT_0) ? 0
     : (res == WAIT_TIMEOUT) ? 1 : -1;
#endif
}

#ifndef _WIN32
#define thread_event_signal_nolock(tev)		(pthread_cond_signal(&(tev)->cond))
#else
#define thread_event_signal_nolock(tev)		(!PulseEvent((tev)->h))
#endif

static int
thread_event_signal (thread_event_t *tev)
{
#ifndef _WIN32
    pthread_mutex_t *csp = &tev->cs;
    int res;

    pthread_mutex_lock(csp);
    res = pthread_cond_signal(&tev->cond);
    pthread_mutex_unlock(csp);

    if (res) errno = res;
    return res;
#else
    return !PulseEvent(tev->h);
#endif
}

