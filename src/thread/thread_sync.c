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
    int signalled;
#else
    HANDLE h;
#endif
} thread_cond_t;


static int
thread_cond_new (thread_cond_t *tcond)
{
#ifndef _WIN32
    int res;

    res = pthread_cond_init(&tcond->cond, NULL);
    if (!res) {
	res = pthread_mutex_init(&tcond->cs, NULL);
	if (res)
	    pthread_cond_destroy(&tcond->cond);
    }
    if (res) errno = res;
    return res;
#else
    tcond->h = CreateEvent(NULL, FALSE, FALSE, NULL);  /* auto-reset */
    return (tcond->h != NULL) ? 0 : -1;
#endif
}

static int
thread_cond_del (thread_cond_t *tcond)
{
    int res;

#ifndef _WIN32
    res = pthread_cond_destroy(&tcond->cond);
    if (!res)
	res = pthread_mutex_destroy(&tcond->cs);
    if (res) errno = res;
#else
    res = 0;
    if (tcond->h) {
	res = !CloseHandle(tcond->h);
	tcond->h = NULL;
    }
#endif
    return res;
}

static int
thread_cond_wait (thread_cond_t *tcond, msec_t timeout)
{
    int res;

#ifndef _WIN32
    pthread_mutex_t *csp = &tcond->cs;

    sys_vm_leave();
    if (timeout == TIMEOUT_INFINITE) {
	pthread_mutex_lock(csp);
	res = tcond->signalled ? 0
	 : pthread_cond_wait(&tcond->cond, &tcond->cs);
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
	res = tcond->signalled ? 0
	 : pthread_cond_timedwait(&tcond->cond, &tcond->cs, &ts);
    }
    tcond->signalled = 0;
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
    res = WaitForSingleObject(tcond->h, timeout);
    sys_vm_enter();

    return (res == WAIT_OBJECT_0) ? 0
     : (res == WAIT_TIMEOUT) ? 1 : -1;
#endif
}

#ifndef _WIN32
#define thread_cond_signal_nolock(tcond)	((tcond)->signalled = 1, pthread_cond_signal(&(tcond)->cond))
#else
#define thread_cond_signal_nolock(tcond)	(!SetEvent((tcond)->h))
#endif

static int
thread_cond_signal (thread_cond_t *tcond)
{
#ifndef _WIN32
    pthread_mutex_t *csp = &tcond->cs;
    int res;

    pthread_mutex_lock(csp);
    tcond->signalled = 1;
    res = pthread_cond_signal(&tcond->cond);
    pthread_mutex_unlock(csp);

    if (res) errno = res;
    return res;
#else
    return !SetEvent(tcond->h);
#endif
}

