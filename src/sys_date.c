/* Lua System: Date & Time */

#define PERIOD_TYPENAME	"sys.period"

#ifndef _WIN32
#if defined(_POSIX_MONOTONIC_CLOCK) && _POSIX_MONOTONIC_CLOCK >= 0
#define USE_CLOCK_GETTIME
#endif
#else
struct period {
    LARGE_INTEGER start, freq;
};
#endif


#ifndef _WIN32
msec_t
get_milliseconds (void)
{
#ifdef USE_CLOCK_GETTIME
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ts.tv_sec * 1000 + ts.tv_nsec / 1000000L);
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
    lua_pushnumber(L, get_milliseconds());
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
 * Returns: [string | date (table)]
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
    const struct tm *tsp;

    tsp = is_UTC ? gmtime(&t) : localtime(&t);
    if (tsp == NULL)  /* invalid date */
	return 0;

    if (lua_istable(L, 1)) {
	lua_settop(L, 1);
	date_setfield(L, "sec", tsp->tm_sec);
	date_setfield(L, "min", tsp->tm_min);
	date_setfield(L, "hour", tsp->tm_hour);
	date_setfield(L, "day", tsp->tm_mday);
	date_setfield(L, "month", tsp->tm_mon + 1);
	date_setfield(L, "year", tsp->tm_year + 1900);
	date_setfield(L, "wday", tsp->tm_wday);
	date_setfield(L, "yday", tsp->tm_yday + 1);
	date_setfield(L, "isdst", tsp->tm_isdst);
    } else {
	char buf[256];
	const char *s = luaL_optstring(L, 1, "%c");
	size_t len = strftime(buf, sizeof(buf), s, tsp);

	if (len)
	    lua_pushlstring(L, buf, len);
	else
	    luaL_error(L, "date: format too long");
    }
    return 1;
}

/*
 * Arguments: [date (table)]
 * Returns: [time (number)]
 */
static int
sys_time (lua_State *L)
{
    time_t t;

    if (lua_istable(L, 1)) {
	struct tm ts;

	lua_settop(L, 1);
	ts.tm_sec = date_getfield(L, "sec", 0);
	ts.tm_min = date_getfield(L, "min", 0);
	ts.tm_hour = date_getfield(L, "hour", 0);
	ts.tm_mday = date_getfield(L, "day", -2);
	ts.tm_mon = date_getfield(L, "month", -2) - 1;
	ts.tm_year = date_getfield(L, "year", -2) - 1900;
	ts.tm_isdst = date_getfield(L, "isdst", -1);
	t = mktime(&ts);
	if (t == (time_t) -1)
	    return sys_seterror(L, 0);
    } else
	t = time(NULL);  /* current time */
    lua_pushnumber(L, t);
    return 1;
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
#ifdef USE_CLOCK_GETTIME
    lua_newuserdata(L, sizeof(struct timespec));
#else
    lua_newuserdata(L, sizeof(struct timeval));
#endif
#else
    struct period *p = lua_newuserdata(L, sizeof(struct period));

    QueryPerformanceFrequency(&p->freq);
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
#ifdef USE_CLOCK_GETTIME
    clock_gettime(CLOCK_MONOTONIC, p);
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
    lua_Number usec;

#ifndef _WIN32
#ifdef USE_CLOCK_GETTIME
    struct timespec te, *ts = checkudata(L, 1, PERIOD_TYPENAME);

    clock_gettime(CLOCK_MONOTONIC, &te);

    te.tv_sec -= ts->tv_sec;
    te.tv_nsec -= ts->tv_nsec;
    if (te.tv_nsec < 0) {
	te.tv_sec--;
	te.tv_nsec += 1000000000L;
    }
    usec = (lua_Number) (te.tv_sec * 1000000000L + te.tv_nsec) / 1000;
#else
    struct timeval te, *ts = checkudata(L, 1, PERIOD_TYPENAME);

    gettimeofday(&te, NULL);

    te.tv_sec -= ts->tv_sec;
    te.tv_usec -= ts->tv_usec;
    if (te.tv_usec < 0) {
	te.tv_sec--;
	te.tv_usec += 1000000L;
    }
    usec = (lua_Number) (te.tv_sec * 1000000L + te.tv_usec);
#endif
#else
    struct period *p = checkudata(L, 1, PERIOD_TYPENAME);
    LARGE_INTEGER stop;

    QueryPerformanceCounter(&stop);
    usec = (lua_Number) (stop.QuadPart - p->start.QuadPart) / p->freq.QuadPart;
#endif
    lua_pushnumber(L, usec);
    return 1;
}


#define DATE_METHODS \
    {"msec",		sys_msec}, \
    {"date",		sys_date}, \
    {"time",		sys_time}, \
    {"difftime",	sys_difftime}, \
    {"period",		sys_period}

static luaL_reg period_meth[] = {
    {"start",	period_start},
    {"get",	period_get},
    {NULL, NULL}
};
