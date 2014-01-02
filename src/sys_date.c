/* Lua System: Date & Time */

#define PERIOD_TYPENAME	"sys.period"

typedef float	period_t;

#ifndef _WIN32

#if defined(_POSIX_MONOTONIC_CLOCK) && (_POSIX_MONOTONIC_CLOCK >= 0)
#ifdef CLOCK_MONOTONIC_COARSE
#define SYS_MONOTONIC_CLOCKID CLOCK_MONOTONIC_COARSE
#else
#define SYS_MONOTONIC_CLOCKID CLOCK_MONOTONIC
#endif
#elif defined(__APPLE__) && defined(__MACH__)
#define SYS_MONOTONIC_MACH
#include <mach/mach.h>
#include <mach/mach_time.h>
#endif

#else

struct period {
  period_t cycle;
  LARGE_INTEGER start;
};

#include "win32/strptime.c"

#endif


#ifndef _WIN32
#ifdef SYS_MONOTONIC_MACH
static uint64_t
sys_absolutetonanos (uint64_t elapsed)
{
  static mach_timebase_info_data_t timebase;

  if (timebase.denom == 0) {
    (void) mach_timebase_info(&timebase);
  }
  return elapsed * timebase.numer / timebase.denom;
}
#endif

msec_t
sys_milliseconds (void)
{
#if defined(SYS_MONOTONIC_CLOCKID)
  struct timespec ts;
  clock_gettime(SYS_MONOTONIC_CLOCKID, &ts);
  return (ts.tv_sec * 1000 + ts.tv_nsec / 1000000L);
#elif defined(SYS_MONOTONIC_MACH)
  return sys_absolutetonanos(mach_absolute_time()) / 1000000L;
#else
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (tv.tv_sec * 1000 + tv.tv_usec / 1000);
#endif
}
#endif

/*
 * Returns: milliseconds (number)
 */
static int
sys_msec (lua_State *L)
{
  lua_pushnumber(L, sys_milliseconds());
  return 1;
}


static void
date_setfield (lua_State *L, const char *key, int value)
{
  if (value == -1)
    lua_pushnil(L);
  else
    lua_pushinteger(L, value);
  lua_setfield(L, -2, key);
}

static int
date_getfield (lua_State *L, const char *key, int value)
{
  int res;

  lua_getfield(L, -1, key);
  if (lua_isnil(L, -1)) {
    if (value == -2)
      luaL_error(L, "date: \"%s\" expected", key);
    res = value;
  } else
    res = lua_tointeger(L, -1);
  lua_pop(L, 1);
  return res;
}

/*
 * Arguments: [format (string) | date (table), time (number), is_UTC (boolean)]
 * Returns: [date (string) | date (table)]
 *
 * Note: date table & format
 *	{ year=%Y, month=%m, day=%d, hour=%H, min=%M, sec=%S,
 *	  wday=%w+1, yday=%j, isdst=? }
 */
static int
sys_date (lua_State *L)
{
  time_t t = lua_isnumber(L, 2) ? (time_t) lua_tointeger(L, 2)
   : time(NULL);  /* current time */
  const int is_UTC = lua_isboolean(L, -1) && lua_toboolean(L, -1);
  const struct tm *tmp;
#if (defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 1) \
    || (defined(_XOPEN_SOURCE) && _XOPEN_SOURCE) \
    || (defined(_BSD_SOURCE) && _BSD_SOURCE) \
    || (defined(_SVID_SOURCE) && _SVID_SOURCE) \
    || (defined(_POSIX_SOURCE) && _POSIX_SOURCE)
  struct tm tm;

  tmp = is_UTC ? gmtime_r(&t, &tm) : localtime_r(&t, &tm);
#else
  tmp = is_UTC ? gmtime(&t) : localtime(&t);
#endif
  if (tmp == NULL)  /* invalid date */
    return 0;

  if (lua_istable(L, 1)) {
    lua_settop(L, 1);
    date_setfield(L, "sec", tmp->tm_sec);
    date_setfield(L, "min", tmp->tm_min);
    date_setfield(L, "hour", tmp->tm_hour);
    date_setfield(L, "day", tmp->tm_mday);
    date_setfield(L, "month", tmp->tm_mon + 1);
    date_setfield(L, "year", tmp->tm_year + 1900);
    date_setfield(L, "wday", tmp->tm_wday);
    date_setfield(L, "yday", tmp->tm_yday + 1);
    date_setfield(L, "isdst", tmp->tm_isdst);
  } else {
    char buf[256];
    const char *s = luaL_optstring(L, 1, "%c");
    size_t len = strftime(buf, sizeof(buf), s, tmp);

    if (len)
      lua_pushlstring(L, buf, len);
    else
      luaL_error(L, "date: format too long");
  }
  return 1;
}

/*
 * Arguments: [date (table)]
 * |
 * Arguments: [date (string), format (string)]
 * Returns: [time (number)]
 */
static int
sys_time (lua_State *L)
{
  time_t t;

  if (lua_isnoneornil(L, 1))
    t = time(NULL);  /* current time */
  else {
    struct tm tm;

    if (lua_istable(L, 1)) {
      lua_settop(L, 1);
      tm.tm_sec = date_getfield(L, "sec", 0);
      tm.tm_min = date_getfield(L, "min", 0);
      tm.tm_hour = date_getfield(L, "hour", 0);
      tm.tm_mday = date_getfield(L, "day", -2);
      tm.tm_mon = date_getfield(L, "month", -2) - 1;
      tm.tm_year = date_getfield(L, "year", -2) - 1900;
      tm.tm_isdst = date_getfield(L, "isdst", -1);
    } else {
      const char *s = luaL_checkstring(L, 1);
      const char *format = luaL_checkstring(L, 2);

      memset(&tm, 0, sizeof(struct tm));
      tm.tm_isdst = -1;
      if (!strptime(s, format, &tm))
        goto err;
    }
    t = mktime(&tm);
    if (t == (time_t) -1)
      goto err;
  }
  lua_pushnumber(L, (lua_Number) t);
  return 1;
 err:
  return sys_seterror(L, 0);
}

/*
 * Arguments: time_later (number), time (number)
 * Returns: number
 */
static int
sys_difftime (lua_State *L)
{
  lua_pushnumber(L, difftime((time_t) lua_tointeger(L, 1),
   (time_t) lua_tointeger(L, 2)));
  return 1;
}


/*
 * Returns: period_udata
 */
static int
sys_period (lua_State *L)
{
#ifndef _WIN32
#ifdef SYS_MONOTONIC_CLOCKID
  lua_newuserdata(L, sizeof(struct timespec));
#elif defined(SYS_MONOTONIC_MACH)
  lua_newuserdata(L, sizeof(uint64_t));
#else
  lua_newuserdata(L, sizeof(struct timeval));
#endif
#else
  struct period *p = lua_newuserdata(L, sizeof(struct period));
  LARGE_INTEGER freq;

  QueryPerformanceFrequency(&freq);
  p->cycle = (period_t) 1000000.0 / freq.QuadPart;
#endif
  luaL_getmetatable(L, PERIOD_TYPENAME);
  lua_setmetatable(L, -2);
  return 1;
}

/*
 * Arguments: period_udata
 * Returns: period_udata
 */
static int
period_start (lua_State *L)
{
  void *p = checkudata(L, 1, PERIOD_TYPENAME);

#ifndef _WIN32
#if defined(SYS_MONOTONIC_CLOCKID)
  clock_gettime(SYS_MONOTONIC_CLOCKID, p);
#elif defined(SYS_MONOTONIC_MACH)
  *((uint64_t *) p) = mach_absolute_time();
#else
  gettimeofday(p, NULL);
#endif
#else
  QueryPerformanceCounter(&((struct period *) p)->start);
#endif
  return 1;
}

/*
 * Arguments: period_udata
 * Returns: microseconds (number)
 */
static int
period_get (lua_State *L)
{
  int64_t elapsed;

#ifndef _WIN32
#if defined(SYS_MONOTONIC_CLOCKID)
  struct timespec te, *ts = checkudata(L, 1, PERIOD_TYPENAME);
  const period_t cycle = 1.0 / 1000.0;

  clock_gettime(SYS_MONOTONIC_CLOCKID, &te);

  te.tv_sec -= ts->tv_sec;
  te.tv_nsec -= ts->tv_nsec;
  if (te.tv_nsec < 0) {
    te.tv_sec--;
    te.tv_nsec += 1000000000L;
  }
  elapsed = (int64_t) te.tv_sec * 1000000000L + te.tv_nsec;
#elif defined(SYS_MONOTONIC_MACH)
  const uint64_t *ts = checkudata(L, 1, PERIOD_TYPENAME);
  const period_t cycle = 1.0 / 1000.0;

  elapsed = (int64_t) mach_absolute_time() - (int64_t) *ts;
  elapsed = (int64_t) sys_absolutetonanos((uint64_t) elapsed);
#else
  struct timeval te, *ts = checkudata(L, 1, PERIOD_TYPENAME);
  const period_t cycle = 1;

  gettimeofday(&te, NULL);

  te.tv_sec -= ts->tv_sec;
  te.tv_usec -= ts->tv_usec;
  if (te.tv_usec < 0) {
    te.tv_sec--;
    te.tv_usec += 1000000L;
  }
  elapsed = (int64_t) te.tv_sec * 1000000L + te.tv_usec;
#endif
#else
  struct period *p = checkudata(L, 1, PERIOD_TYPENAME);
  LARGE_INTEGER stop;
  const period_t cycle = p->cycle;

  QueryPerformanceCounter(&stop);
  elapsed = (int64_t) stop.QuadPart - (int64_t) p->start.QuadPart;
#endif
  lua_pushnumber(L, (lua_Number) ((period_t) elapsed * cycle));
  return 1;
}


#define DATE_METHODS \
  {"msec",	sys_msec}, \
  {"date",	sys_date}, \
  {"time",	sys_time}, \
  {"difftime",	sys_difftime}, \
  {"period",	sys_period}

static luaL_Reg period_meth[] = {
  {"start",	period_start},
  {"get",	period_get},
  {NULL, NULL}
};
