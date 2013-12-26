/* Lua System: Threading: Synchronization */

#if !defined(_WIN32) || (defined(SRWLOCK_INIT) && defined(WIN32_VISTA))
#define USE_PTHREAD_SYNC
#endif


/* Critical Section */
#if !defined(_WIN32)
typedef pthread_mutex_t		thread_critsect_t;
#elif defined(USE_PTHREAD_SYNC)
typedef SRWLOCK			thread_critsect_t;
#else
typedef CRITICAL_SECTION	thread_critsect_t;
#endif


static int
thread_critsect_new (thread_critsect_t *tcs)
{
#if !defined(_WIN32)
  const int res = pthread_mutex_init(tcs, NULL);
  if (res) errno = res;
  return res;
#elif defined(USE_PTHREAD_SYNC)
  InitializeSRWLock(tcs);
  return 0;
#else
  return !InitCriticalSection(tcs);
#endif
}

#if !defined(_WIN32)
#define thread_critsect_del(tcs)	pthread_mutex_destroy(tcs)
#elif defined(USE_PTHREAD_SYNC)
#define thread_critsect_del(tcs)	((void) (tcs))
#else
#define thread_critsect_del(tcs)	DeleteCriticalSection(tcs)
#endif

#if !defined(_WIN32)
#define thread_critsect_enter(tcs)	pthread_mutex_lock(tcs)
#elif defined(USE_PTHREAD_SYNC)
#define thread_critsect_enter(tcs)	(AcquireSRWLockExclusive(tcs), 0)
#else
#define thread_critsect_enter(tcs)	EnterCriticalSection(tcs)
#endif

#if !defined(_WIN32)
#define thread_critsect_leave(tcs)	pthread_mutex_unlock(tcs)
#elif defined(USE_PTHREAD_SYNC)
#define thread_critsect_leave(tcs)	(ReleaseSRWLockExclusive(tcs), 0)
#else
#define thread_critsect_leave(tcs)	LeaveCriticalSection(tcs)
#endif


/* Condition */
#if !defined(_WIN32)
typedef pthread_cond_t		thread_cond_t;
#elif defined(USE_PTHREAD_SYNC)
typedef CONDITION_VARIABLE	thread_cond_t;
#else
typedef HANDLE			thread_cond_t;
#endif


static int
thread_cond_new (thread_cond_t *tcond)
{
#if !defined(_WIN32)
  const int res = pthread_cond_init(tcond, NULL);
  if (res) errno = res;
  return res;
#elif defined(USE_PTHREAD_SYNC)
  InitializeConditionVariable(tcond);
  return 0;
#else
  *tcond = CreateEvent(NULL, FALSE, FALSE, NULL);  /* auto-reset */
  return (*tcond != NULL) ? 0 : -1;
#endif
}

#if !defined(_WIN32)
#define thread_cond_del(tcond)		pthread_cond_destroy(tcond)
#elif defined(USE_PTHREAD_SYNC)
#define thread_cond_del(tcond)		((void) (tcond), 0)
#else
#define thread_cond_del(tcond)		CloseHandle(*tcond)
#endif

#if !defined(_WIN32)
#define thread_cond_signal(tcond)	pthread_cond_signal(tcond)
#elif defined(USE_PTHREAD_SYNC)
#define thread_cond_signal(tcond)	(WakeConditionVariable(tcond), 0)
#else
#define thread_cond_signal(tcond)	!SetEvent(*tcond)
#endif

#if defined(USE_PTHREAD_SYNC)

#if !defined(_WIN32)

static void
thread_timespec (struct timespec *ts, const msec_t timeout)
{
  struct timeval tv;

  gettimeofday(&tv, NULL);
  tv.tv_sec += timeout / 1000;
  tv.tv_usec += (timeout % 1000) * 1000;
  if (tv.tv_usec >= 1000000) {
    tv.tv_sec++;
    tv.tv_usec -= 1000000;
  }

  ts->tv_sec = tv.tv_sec;
  ts->tv_nsec = tv.tv_usec * 1000;
}

#endif

static int
thread_cond_wait_nolock (thread_cond_t *condp, thread_critsect_t *csp,
                         const msec_t timeout)
{
#if !defined(_WIN32)
  int res;

  if (timeout == TIMEOUT_INFINITE) {
    res = pthread_cond_wait(condp, csp);
  } else {
    struct timespec ts;
    thread_timespec(&ts, timeout);

    res = pthread_cond_timedwait(condp, csp, &ts);
  }

  if (res) {
    if (res == ETIMEDOUT)
      return 1;
    errno = res;
    return -1;
  }
#else
  if (!SleepConditionVariableSRW(condp, csp, timeout, 0)) {
    return (GetLastError() == ERROR_TIMEOUT) ? 1 : -1;
  }
#endif
  return 0;
}

static int
thread_cond_wait_value (thread_cond_t *condp, thread_critsect_t *csp,
                        volatile unsigned int *signalled,
                        const unsigned int test_value,
                        const int reset, const msec_t timeout)
{
  int res = 0;

#if !defined(_WIN32)
  if (timeout == TIMEOUT_INFINITE) {
    thread_critsect_enter(csp);
    while (*signalled != test_value && !res)
      res = pthread_cond_wait(condp, csp);
  } else {
    struct timespec ts;
    thread_timespec(&ts, timeout);

    thread_critsect_enter(csp);
    while (*signalled != test_value && !res)
      res = pthread_cond_timedwait(condp, csp, &ts);
  }
  if (!res && reset) *signalled = 0;
  thread_critsect_leave(csp);

  if (res) {
    if (res == ETIMEDOUT)
      return 1;
    errno = res;
    return -1;
  }
#else
  thread_critsect_enter(csp);
  while (*signalled != test_value && !res)
    res = !SleepConditionVariableSRW(condp, csp, timeout, 0);
  if (!res && reset) *signalled = 0;
  thread_critsect_leave(csp);

  if (res) return (GetLastError() == ERROR_TIMEOUT) ? 1 : -1;
#endif
  return 0;
}

#else  /* Win32 */

static int
thread_cond_wait_nolock (thread_cond_t *condp, thread_critsect_t *csp,
                         const msec_t timeout)
{
  int res;

  thread_critsect_leave(csp);
  res = WaitForSingleObject(*condp, timeout);
  thread_critsect_enter(csp);

  return (res == WAIT_OBJECT_0) ? 0
   : (res == WAIT_TIMEOUT) ? 1 : -1;
}

#if 0
static int
thread_cond_wait_value (thread_cond_t *condp, thread_critsect_t *csp,
                        volatile unsigned int *signalled,
                        const unsigned int test_value,
                        const int reset, const msec_t timeout)
{
  int res = WAIT_OBJECT_0;

  (void) csp;

  while (*signalled != test_value && res == WAIT_OBJECT_0) {
    res = WaitForSingleObject(*condp, timeout);
  }
  if (res == WAIT_OBJECT_0) {
    if (reset) *signalled = 0;
    return 0;
  }
  return (res == WAIT_TIMEOUT) ? 1 : -1;
}
#endif

#endif  /* !defined(USE_PTHREAD_SYNC) */

#ifdef _WIN32

static int
thread_handle_wait (HANDLE h, const msec_t timeout)
{
  const int res = WaitForSingleObject(h, timeout);

  return (res == WAIT_OBJECT_0) ? 0
   : (res == WAIT_TIMEOUT) ? 1 : -1;
}

#endif


/* Event */
typedef struct {
  thread_cond_t cond;
#if defined(USE_PTHREAD_SYNC)
  thread_critsect_t cs;
#define THREAD_EVENT_SIGNALLED	1
  unsigned int volatile signalled;
#endif
} thread_event_t;


static int
thread_event_new (thread_event_t *tev)
{
  int res = thread_cond_new(&tev->cond);
#if defined(USE_PTHREAD_SYNC)
  if (!res) {
    res = thread_critsect_new(&tev->cs);
    if (res) {
      (void) thread_cond_del(&tev->cond);
    }
  }
#endif
  return res;
}

static int
thread_event_del (thread_event_t *tev)
{
#if defined(USE_PTHREAD_SYNC)
  thread_critsect_del(&tev->cs);
#endif
  return thread_cond_del(&tev->cond);
}

static int
thread_event_wait (thread_event_t *tev, struct sys_thread *td,
                   const msec_t timeout)
{
  int res;

  sys_vm2_leave(td);
#if defined(USE_PTHREAD_SYNC)
  res = thread_cond_wait_value(&tev->cond, &tev->cs,
   &tev->signalled, THREAD_EVENT_SIGNALLED, 1, timeout);
#else
  res = thread_handle_wait(tev->cond, timeout);
#endif
  sys_vm2_enter(td);
  return res;
}

#if defined(USE_PTHREAD_SYNC)
#define thread_event_signal_nolock(tev) \
    ((tev)->signalled = THREAD_EVENT_SIGNALLED, \
     thread_cond_signal(&(tev)->cond))
#else
#define thread_event_signal_nolock(tev)		!SetEvent((tev)->cond)
#endif

static int
thread_event_signal (thread_event_t *tev)
{
#if defined(USE_PTHREAD_SYNC)
  thread_critsect_t *csp = &tev->cs;
  int res;

  thread_critsect_enter(csp);
  tev->signalled = THREAD_EVENT_SIGNALLED;
  res = thread_cond_signal(&tev->cond);
  thread_critsect_leave(csp);

  if (res) errno = res;
  return res;
#else
  return !SetEvent(tev->cond);
#endif
}

