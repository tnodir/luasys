/* Lua System: Threading: CPU Affinity */

#if !defined(_WIN32) && defined(CPU_SET)

#define USE_PTHREAD_AFFIN

typedef cpu_set_t	affin_mask_t;

#else

typedef size_t		affin_mask_t;

#define CPU_SETSIZE	sizeof(affin_mask_t)
#define CPU_ZERO(p)	(*(p) = 0)
#define CPU_SET(i,p)	(*(p) |= (1 << (i)))
#define CPU_ISSET(i,p)	((*(p) & (1 << (i))) != 0)

#endif


static int
getcpucount (affin_mask_t *mp)
{
    int n = 0;
    unsigned int i;

    for (i = 0; i < CPU_SETSIZE; ++i) {
	if (CPU_ISSET(i, mp)) ++n;
    }
    return n;
}

static void
getcpumask (affin_mask_t *out, affin_mask_t *mp, int cpu)
{
    unsigned int i;

    CPU_ZERO(out);
    for (i = 0; i < CPU_SETSIZE; ++i) {
	if (CPU_ISSET(i, mp) && !--cpu)
	    break;
    }
    CPU_SET(i, out);
}

static int
thread_affin_get_mask (affin_mask_t *mp)
{
#if defined(USE_PTHREAD_AFFIN)
    return sched_getaffinity(getpid(), sizeof(affin_mask_t), mp);
#elif defined(_WIN32)
    const HANDLE hProc = GetCurrentProcess();
    DWORD_PTR proc_mask, sys_mask;

    if (GetProcessAffinityMask(hProc, &proc_mask, &sys_mask)) {
	*mp = proc_mask;
	return 0;
    }
    return -1;
#else
    return -1;
#endif
}

static int
thread_affin_set_cpu (struct sys_thread *td, int cpu)
{
    affin_mask_t proc_mask, thread_mask;

    if (!thread_affin_get_mask(&proc_mask)) {
	affin_mask_t *mp = &thread_mask;

	if (cpu) getcpumask(mp, &proc_mask, cpu);
	else mp = &proc_mask;

#if defined(USE_PTHREAD_AFFIN)
	return pthread_setaffinity_np(td->tid, sizeof(affin_mask_t), mp);
#elif defined(_WIN32)
	return (SetThreadAffinityMask(td->th, *mp) > 0) ? 0 : -1;
#endif
    }
    return -1;
}

static int
thread_affin_run (struct sys_thread *td, const int is_affin)
{
#ifndef _WIN32
    pthread_attr_t attr;
    int res;

    if ((res = pthread_attr_init(&attr)))
	goto err;
    if ((res = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED))
     || (res = pthread_attr_setstacksize(&attr, td->vmtd->stack_size))) {
	pthread_attr_destroy(&attr);
	goto err;
    }

    res = pthread_create(&td->tid, &attr, (thread_func_t) thread_start, td);
    pthread_attr_destroy(&attr);
    if (!res) {
#if defined(USE_PTHREAD_AFFIN)
	if (is_affin)
	    thread_affin_set_cpu(td, td->vmtd->cpu);
#endif
	return 0;
    }
 err:
    errno = res;
#else
    const unsigned long hThr = _beginthreadex(NULL, td->vmtd->stack_size,
     (thread_func_t) thread_start, td, 0, (unsigned int *) &td->tid);

    (void) is_affin;

    if (hThr) {
	td->th = (HANDLE) hThr;
	if (is_WinNT && td->vmtd->cpu)
	    thread_affin_set_cpu(td, td->vmtd->cpu);
	return 0;
    }
#endif
    return -1;
}

/*
 * Returns: number_of_processors (number)
 */
static int
thread_affin_nprocs (lua_State *L)
{
    affin_mask_t mask;
    int n = 0;

    if (!thread_affin_get_mask(&mask)) {
	n = getcpucount(&mask);
    }
    lua_pushinteger(L, n);
    return 1;
}


#define AFFIN_METHODS \
    {"nprocs",	thread_affin_nprocs}
