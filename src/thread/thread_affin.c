/* Lua System: Threading: CPU Affinity */

#if defined(__linux__)

typedef cpu_set_t	affin_mask_t;

#define USE_PTHREAD_AFFIN

#ifndef CPU_ALLOC
#define CPU_ALLOC(n)	malloc(n / CHAR_BIT)
#define CPU_FREE(p)	free(p)
#endif
#define CPU_NEW()	CPU_ALLOC(CPU_SETSIZE)
#define CPU_DEL(p)	CPU_FREE(p)
#define CPU_SIZEOF(p)	sizeof(affin_mask_t)
#define CPU_COUNTMAX(p)	CPU_SETSIZE

#elif defined(__FreeBSD__)

#include <pthread_np.h>

typedef cpuset_t	affin_mask_t;

#define USE_PTHREAD_AFFIN

#define CPU_NEW()	malloc(sizeof(affin_mask_t))
#define CPU_DEL(p)	free(p)
#define CPU_SIZEOF(p)	CPU_SETSIZE

#elif defined(BSD)

typedef cpuset_t	affin_mask_t;

#define USE_PTHREAD_AFFIN

#define CPU_NEW()	cpuset_create()
#define CPU_DEL(p)	cpuset_destroy(p)
#define CPU_SIZEOF(p)	cpuset_size(p)
#define CPU_ZERO(p)	cpuset_zero(p)
#define CPU_SET(i,p)	cpuset_set(i, p)
#define CPU_ISSET(i,p)	(cpuset_isset(i, p) > 0)

#else  /* _WIN32 */

typedef size_t		affin_mask_t;

#define CPU_NEW()	malloc(sizeof(affin_mask_t))
#define CPU_DEL(p)	free(p)
#define CPU_SIZEOF(p)	sizeof(affin_mask_t)
#define CPU_ZERO(p)	(*(p) = 0)
#define CPU_SET(i,p)	(*(p) |= (1 << (i)))
#define CPU_ISSET(i,p)	((*(p) & (1 << (i))) != 0)

#endif


#ifndef CPU_COUNTMAX
#define CPU_COUNTMAX(p)	(CPU_SIZEOF(p) * CHAR_BIT)
#endif

#ifndef CPU_COUNT
static int
affin_cpu_count (affin_mask_t *mp)
{
  const unsigned int nmax = CPU_COUNTMAX(mp);
  unsigned int i;
  int n = 0;

  for (i = 0; i < nmax; ++i) {
    if (CPU_ISSET(i, mp)) ++n;
  }
  return n;
}

#define CPU_COUNT(p)	affin_cpu_count(p)
#endif

static int
affin_cpu_offset (int cpu, affin_mask_t *mp)
{
  const unsigned int nmax = CPU_COUNTMAX(mp);
  unsigned int i;

  for (i = 0; i < nmax; ++i) {
    if (CPU_ISSET(i, mp) && !--cpu)
      return (int) i;
  }
  return -1;
}

static int
affin_cpu_fill (affin_mask_t *mp)
{
#if defined(USE_PTHREAD_AFFIN)
  const int res = pthread_getaffinity_np(pthread_self(), CPU_SIZEOF(mp), mp);
  if (res) errno = res;
  return res;
#elif defined(_WIN32)
  {
    const HANDLE hProc = GetCurrentProcess();
    DWORD_PTR proc_mask, sys_mask;

    if (GetProcessAffinityMask(hProc, &proc_mask, &sys_mask)) {
      *mp = proc_mask;
      return 0;
    }
  }
  return -1;
#else
  (void) mp;

  return -1;
#endif
}

static int
affin_cpu_set (thread_id_t tid, int cpu)
{
  affin_mask_t *mp = CPU_NEW();
  int res = -1;

  if (!mp) goto err;

  if (!affin_cpu_fill(mp)) {
    if (cpu) {
      const int cpu_off = affin_cpu_offset(cpu, mp);

      if (cpu_off == -1) goto err_clean;

      CPU_ZERO(mp);
      CPU_SET(cpu_off, mp);
    }

#if defined(USE_PTHREAD_AFFIN)
    res = pthread_setaffinity_np(tid, CPU_SIZEOF(mp), mp);
    if (res) errno = res;
#elif defined(_WIN32)
    res = (SetThreadAffinityMask(tid, *mp) > 0) ? 0 : -1;
#endif
  }
 err_clean:
  CPU_DEL(mp);
 err:
  return res;
}

/*
 * Returns: [number_of_processors (number)]
 */
static int
affin_nprocs (lua_State *L)
{
  affin_mask_t *mp = CPU_NEW();
  int n = 0;

  if (mp) {
    if (!affin_cpu_fill(mp)) {
      n = CPU_COUNT(mp);
    }
    CPU_DEL(mp);
  }
  if (!n) return 0;
  lua_pushinteger(L, n);
  return 1;
}


#define AFFIN_METHODS \
  {"nprocs",	affin_nprocs}
