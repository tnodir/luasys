/* Lua System: Threading: Data Pool */

#define DPOOL_TYPENAME	"sys.thread.data_pool"

struct data_pool {
    unsigned int volatile n;  /* count of data in storage */

    int volatile nwaits;  /* number of blocked readers */
    int volatile nput; /* number of items to put as new data */
    struct sys_thread * volatile td;  /* data writer */

    unsigned int idx, top;  /* storage indexes */
    unsigned int max;  /* maximum watermark of data */

#define DPOOL_PUTONFULL		1
#define DPOOL_GETONEMPTY	2
    unsigned int flags;

    thread_event_t tev;  /* synchronization */
    sys_trigger_t trigger;  /* notify event_queue */
};


/*
 * Returns: [dpool_udata]
 */
static int
thread_data_pool (lua_State *L)
{
    struct data_pool *dp = lua_newuserdata(L, sizeof(struct data_pool));
    memset(dp, 0, sizeof(struct data_pool));
    dp->max = (unsigned int) -1;

    if (!thread_event_new(&dp->tev)) {
	luaL_getmetatable(L, DPOOL_TYPENAME);
	lua_setmetatable(L, -2);

	lua_newtable(L);  /* data and callbacks storage */
	lua_setfenv(L, -2);
	return 1;
    }
    return sys_seterror(L, 0);
}

/*
 * Arguments: dpool_udata
 */
static int
dpool_done (lua_State *L)
{
    struct data_pool *dp = checkudata(L, 1, DPOOL_TYPENAME);

    thread_event_del(&dp->tev);
    return 0;
}

/*
 * Arguments: dpool_udata, data_items (any) ...
 */
static int
dpool_put (lua_State *L)
{
    struct sys_thread *td = sys_get_thread();
    struct data_pool *dp = checkudata(L, 1, DPOOL_TYPENAME);
    int nput = lua_gettop(L) - 1;

    if (!td) luaL_argerror(L, 0, "Threading not initialized");
    if (!nput) luaL_argerror(L, 2, "data expected");

    lua_getfenv(L, 1);  /* storage */
    lua_insert(L, 1);

    if (dp->n >= dp->max) {
	if (dp->flags & DPOOL_PUTONFULL) {
	    lua_pushlightuserdata(L, (void *) DPOOL_PUTONFULL);
	    lua_rawget(L, 1);
	    lua_insert(L, 2);
	    lua_call(L, 1 + nput, LUA_MULTRET);
	    nput = lua_gettop(L) - 1;
	    if (!nput) return 0;
	} else do {
	    if (thread_event_wait(&dp->tev, TIMEOUT_INFINITE))
		return sys_seterror(L, 0);
	} while (dp->n >= dp->max);
    }

    /* Try directly move data between threads */
    if (dp->nwaits && !dp->td) {
	dp->td = td;
	dp->nput = nput;
	thread_event_signal(&dp->tev);
	thread_yield(L);
	dp->td = NULL;
	if (!dp->nput) return 0;  /* moved to thread */
	dp->nput = 0;
    }

    /* Keep data in the storage */
    {
	int top = dp->top;

	lua_pushinteger(L, nput);
	do {
	    lua_rawseti(L, 1, ++top);
	} while (nput--);
	dp->top = top;

	/* notify event_queue */
	if (!dp->n++ && dp->trigger)
	    sys_trigger_notify(&dp->trigger, SYS_EVREAD);

	thread_event_signal(&dp->tev);
    }
    return 0;
}

/*
 * Arguments: dpool_udata, [timeout (milliseconds)]
 * Returns: data_items (any) ...
 */
static int
dpool_get (lua_State *L)
{
    struct data_pool *dp = checkudata(L, 1, DPOOL_TYPENAME);
    const msec_t timeout = lua_isnoneornil(L, 2)
     ? TIMEOUT_INFINITE : (msec_t) lua_tointeger(L, 2);
    int nput;

    lua_settop(L, 1);
    lua_getfenv(L, 1);  /* storage */
    lua_insert(L, 1);

    if ((dp->flags & DPOOL_GETONEMPTY) && !dp->n) {
	lua_pushlightuserdata(L, (void *) DPOOL_GETONEMPTY);
	lua_rawget(L, 1);
	lua_insert(L, 2);
	lua_call(L, 1, LUA_MULTRET);
	nput = lua_gettop(L) - 1;
	if (nput) return nput;
    }

    for (; ; ) {
	/* get from storage */
	if (dp->n) {
	    const int idx = dp->idx + 1;
	    int i;

	    lua_rawgeti(L, 1, idx);
	    nput = lua_tointeger(L, -1);
	    lua_pushnil(L);
	    lua_rawseti(L, 1, idx);
	    dp->idx = idx + nput;
	    for (i = dp->idx; i > idx; --i) {
		lua_rawgeti(L, 1, i);
		lua_pushnil(L);
		lua_rawseti(L, 1, i);
	    }
	    if (dp->idx == dp->top)
		dp->idx = dp->top = 0;
	    if (dp->n-- == dp->max) {
		/* notify event_queue */
		if (dp->trigger)
		    sys_trigger_notify(&dp->trigger, SYS_EVWRITE);
		thread_event_signal(&dp->tev);
	    }
	    return nput;
	}

	/* wait signal */
	{
	    int res;
	    dp->nwaits++;
	    res = thread_event_wait(&dp->tev, timeout);
	    dp->nwaits--;
	    if (res) {
		if (res == 1) {
		    lua_pushboolean(L, 0);
		    return 1;  /* timed out */
		}
		return sys_seterror(L, 0);
	    }
	}

	/* get directly from another thread */
	nput = dp->nput;
	if (nput) {
	    dp->nput = 0;
	    lua_xmove(dp->td->L, L, nput);
	    return nput;
	}
    }
}

/*
 * Arguments: dpool_udata, [timeout (milliseconds)]
 * Returns: [signalled/timedout (boolean)]
 */
static int
dpool_wait (lua_State *L)
{
    struct data_pool *dp = checkudata(L, 1, DPOOL_TYPENAME);
    const msec_t timeout = lua_isnoneornil(L, 2)
     ? TIMEOUT_INFINITE : (msec_t) lua_tointeger(L, 2);
    int res;

    res = thread_event_wait(&dp->tev, timeout);
    if (res >= 0) {
	lua_pushboolean(L, !res);
	return 1;
    }
    return sys_seterror(L, 0);
}

/*
 * Arguments: dpool_udata, [maximum (number)]
 * Returns: dpool_udata | maximum (number)
 */
static int
dpool_max (lua_State *L)
{
    struct data_pool *dp = checkudata(L, 1, DPOOL_TYPENAME);

    if (lua_isnoneornil(L, 2))
	lua_pushinteger(L, dp->max);
    else {
	dp->max = luaL_checkinteger(L, 2);
	lua_settop(L, 1);
    }
    return 1;
}

/*
 * Arguments: dpool_udata, [put_on_full (function), get_on_empty (function)]
 */
static int
dpool_callbacks (lua_State *L)
{
    struct data_pool *dp = checkudata(L, 1, DPOOL_TYPENAME);

    lua_settop(L, 3);
    lua_getfenv(L, 1);  /* storage of callbacks */

    dp->flags &= ~(DPOOL_PUTONFULL | DPOOL_GETONEMPTY);
    dp->flags |= (lua_isfunction(L, 2) ? DPOOL_PUTONFULL : 0)
     | (lua_isfunction(L, 3) ? DPOOL_GETONEMPTY : 0);

    lua_pushlightuserdata(L, (void *) DPOOL_PUTONFULL);
    lua_pushvalue(L, 2);
    lua_rawset(L, -3);

    lua_pushlightuserdata(L, (void *) DPOOL_GETONEMPTY);
    lua_pushvalue(L, 3);
    lua_rawset(L, -3);

    return 0;
}

/*
 * Arguments: dpool_udata
 * Returns: number
 */
static int
dpool_count (lua_State *L)
{
    struct data_pool *dp = checkudata(L, 1, DPOOL_TYPENAME);

    lua_pushinteger(L, dp->n);
    return 1;
}

/*
 * Arguments: dpool_udata
 * Returns: string
 */
static int
dpool_tostring (lua_State *L)
{
    struct data_pool *dp = checkudata(L, 1, DPOOL_TYPENAME);
    lua_pushfstring(L, DPOOL_TYPENAME " (%p)", &dp->tev);
    return 1;
}

/*
 * Arguments: ..., dpool_udata
 */
static sys_trigger_t *
dpool_get_trigger (lua_State *L, struct sys_thread **tdp)
{
    struct data_pool *dp = checkudata(L, -1, DPOOL_TYPENAME);

    *tdp = NULL;
    return &dp->trigger;
}


static luaL_reg dpool_meth[] = {
    {"put",		dpool_put},
    {"get",		dpool_get},
    {"wait",		dpool_wait},
    {"max",		dpool_max},
    {"callbacks",	dpool_callbacks},
    {"__len",		dpool_count},
    {"__tostring",	dpool_tostring},
    {"__gc",		dpool_done},
    {NULL, NULL}
};
