/* Lua System: Threading: CPU Affinity: Mac OS X */

#include <mach/thread_act.h>

#define USE_MACH_AFFIN


static int
affin_cpu_set (mach_port_t tid, int cpu)
{
  struct thread_affinity_policy ap;
  int res;

  ap.affinity_tag = cpu;

  res = thread_policy_set(tid, THREAD_AFFINITY_POLICY,
   (thread_policy_t) &ap, THREAD_AFFINITY_POLICY_COUNT);

  return (res == KERN_SUCCESS) ? 0 : -1;
}

/*
 * Returns: number_of_processors (number)
 */
static int
affin_nprocs (lua_State *L)
{
  const int n = sysconf(_SC_NPROCESSORS_ONLN);
  if (n < 1) return 0;
  lua_pushinteger(L, n);
  return 1;
}


#define AFFIN_METHODS \
  {"nprocs",	affin_nprocs}
