/* Lua System: Event Queue */

#define EVQ_TYPENAME	"sys.evq"

#define levq_toevent(L,i) \
    (lua_type(L, (i)) == LUA_TLIGHTUSERDATA ? lua_touserdata(L, (i)) : NULL)


static const int sig_flags[] = {
    EVQ_SIGINT, EVQ_SIGHUP, EVQ_SIGQUIT, EVQ_SIGTERM
};

static const char *const sig_names[] = {
    "INT", "HUP", "QUIT", "TERM", NULL
};


/* Find the log base 2 of an N-bit integer in O(lg(N)) operations with multiply and lookup */
static int
getmaxbit (unsigned int v)
{
    static const int bit_position[32] = {
	0, 9, 1, 10, 13, 21, 2, 29, 11, 14, 16, 18, 22, 25, 3, 30,
	8, 12, 20, 28, 15, 17, 24, 7, 19, 27, 23, 6, 26, 5, 4, 31
    };

    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;

    return bit_position[(unsigned int) (v * 0x07C4ACDDU) >> 27];
}


/*
 * Returns: [evq_udata]
 */
static int
levq_new (lua_State *L)
{
    struct event_queue *evq = lua_newuserdata(L, sizeof(struct event_queue));

    memset(evq, 0, sizeof(struct event_queue));

    lua_assert(sizeof(struct event) >= sizeof(struct timeout_queue));

    if (!evq_init(evq)) {
	luaL_getmetatable(L, EVQ_TYPENAME);
	lua_setmetatable(L, -2);

	lua_newtable(L);  /* environ. */
	lua_newtable(L);  /* {ev_id => obj_udata} */
	lua_rawseti(L, -2, EVQ_OBJ_UDATA);
	lua_newtable(L);  /* {ev_id => cb_func} */
	lua_rawseti(L, -2, EVQ_CALLBACK);
	lua_setfenv(L, -2);
	return 1;
    }
    return sys_seterror(L, 0);
}

/*
 * Arguments: evq_udata
 */
static int
levq_done (lua_State *L)
{
    struct event_queue *evq = checkudata(L, 1, EVQ_TYPENAME);
    struct event *buffers[EVQ_BUF_MAX + 1];  /* cache */

    memset(buffers, 0, sizeof(buffers));

#undef ARG_LAST
#define ARG_LAST	1

    lua_settop(L, ARG_LAST);
    lua_getfenv(L, 1);
    lua_rawgeti(L, ARG_LAST+1, EVQ_OBJ_UDATA);

    /* delete object events */
    lua_pushnil(L);
    while (lua_next(L, -2)) {
	const int ev_id = lua_tointeger(L, -2);
	const int buf_idx = getmaxbit((ev_id | ((1 << EVQ_BUF_IDX) - 1)) + 1);
	const int nmax = (1 << buf_idx);
	struct event *ev = buffers[buf_idx];

	if (!ev) {
	    lua_rawgeti(L, ARG_LAST+1, buf_idx);
	    ev = lua_touserdata(L, -1);
	    lua_pop(L, 1);  /* pop events buffer */
	    buffers[buf_idx] = ev;
	}
	ev += ev_id - ((nmax - 1) & ~((1 << EVQ_BUF_IDX) - 1));

	if (!event_deleted(ev))
	    evq_del(ev, 0);
	lua_pop(L, 1);  /* pop value */
    }

    evq_done(evq);
    return 0;
}


/*
 * Arguments: ..., EVQ_ENVIRON (table)
 */
static struct event *
levq_new_event (lua_State *L, int idx, struct event_queue *evq)
{
    struct event *ev;
    int ev_id;

    ev = evq->ev_free;
    if (ev) {
	evq->ev_free = ev->next_ready;
	ev_id = ev->ev_id;
    } else {
	const int n = evq->buf_nevents;
	const int buf_idx = evq->buf_index + EVQ_BUF_IDX;
	const int nmax = (1 << buf_idx);

	lua_rawgeti(L, idx, buf_idx);
	ev = lua_touserdata(L, -1);
	lua_pop(L, 1);
	if (ev) {
	    ev += n;
	    if (++evq->buf_nevents >= nmax) {
		evq->buf_nevents = 0;
		evq->buf_index++;
	    }
	} else {
	    if (buf_idx > EVQ_BUF_MAX)
		luaL_argerror(L, 1, "too many events");
	    ev = lua_newuserdata(L, nmax * sizeof(struct event));
	    lua_rawseti(L, idx, buf_idx);
	    evq->buf_nevents = 1;
	}
	ev_id = n + ((nmax - 1) & ~((1 << EVQ_BUF_IDX) - 1));
    }
    memset(ev, 0, sizeof(struct event));
    ev->ev_id = ev_id;
    return ev;
}

/*
 * Arguments: ..., EVQ_ENVIRON (table), EVQ_OBJ_UDATA (table), EVQ_CALLBACK (table)
 */
static void
levq_del_event (lua_State *L, int idx, struct event_queue *evq, struct event *ev)
{
    const int ev_id = ev->ev_id;

    /* cb_fun */
    if (ev->flags & EVENT_CALLBACK) {
	lua_pushnil(L);
	lua_rawseti(L, idx + 2, ev_id);
    }
    /* obj_udata */
    lua_pushnil(L);
    lua_rawseti(L, idx + 1, ev_id);

    ev->next_ready = evq->ev_free;
    evq->ev_free = ev;
}


/*
 * Arguments: evq_udata, obj_udata,
 *	events (string: "r", "w", "rw") | signal (number),
 *	callback (function),
 *	[timeout (milliseconds), one_shot (boolean),
 *	event_flags (number)]
 * Returns: [ev_ludata]
 */
static int
levq_add (lua_State *L)
{
    struct event_queue *evq = checkudata(L, 1, EVQ_TYPENAME);
    fd_t *fdp = lua_touserdata(L, 2);
    const char *evstr = (lua_type(L, 3) == LUA_TNUMBER)
     ? NULL : lua_tostring(L, 3);
    const int signo = evstr ? 0 : lua_tointeger(L, 3);
    const msec_t timeout = lua_isnoneornil(L, 5)
     ? TIMEOUT_INFINITE : (msec_t) lua_tointeger(L, 5);
    const unsigned int ev_flags = lua_tointeger(L, 7)
     | (lua_toboolean(L, 6) ? EVENT_ONESHOT : 0)
     | (lua_isnil(L, 4) ? 0 : (EVENT_CALLBACK
     | (lua_isthread(L, 4) ? EVENT_CALLBACK_THREAD : 0)));
    struct event *ev;
    int res;

#undef ARG_LAST
#define ARG_LAST	4

    lua_settop(L, ARG_LAST);
    lua_getfenv(L, 1);
    lua_rawgeti(L, ARG_LAST+1, EVQ_OBJ_UDATA);
    lua_rawgeti(L, ARG_LAST+1, EVQ_CALLBACK);

    ev = levq_new_event(L, ARG_LAST+1, evq);
    ev->fd = fdp ? *fdp : (fd_t) signo;
    ev->flags = (!evstr ? EVENT_READ : (evstr[0] == 'r') ? EVENT_READ
     | (evstr[1] ? EVENT_WRITE : 0) : EVENT_WRITE)
     | ev_flags;

    /* place for timeout_queue */
    if (!evq->ev_free)
	evq->ev_free = levq_new_event(L, ARG_LAST+1, evq);

    if (ev_flags & EVENT_TIMER) {
	res = evq_add_timer(evq, ev, timeout);
    } else {
	if (ev_flags & EVENT_DIRWATCH) {
	    const char *path = luaL_checkstring(L, 2);
	    res = evq_add_dirwatch(evq, ev, path);
	} else {
	    res = evq_add(evq, ev);
	}

	if (!res && timeout != TIMEOUT_INFINITE) {
	    evq_set_timeout(ev, timeout);
	}
    }
    if (!res) {
	const int ev_id = ev->ev_id;

	lua_pushlightuserdata(L, ev);

	/* cb_fun */
	if (ev_flags & EVENT_CALLBACK) {
	    lua_pushvalue(L, 4);
	    lua_rawseti(L, ARG_LAST+3, ev_id);
	}
	/* obj_udata */
	lua_pushvalue(L, 2);
	lua_rawseti(L, ARG_LAST+2, ev_id);

	return 1;
    }
    levq_del_event(L, ARG_LAST+1, evq, ev);
    return sys_seterror(L, 0);
}

/*
 * Arguments: evq_udata, callback (function), timeout (milliseconds),
 *	[object (any)]
 * Returns: [ev_ludata]
 */
static int
levq_add_timer (lua_State *L)
{
    lua_settop(L, 4);
    lua_insert(L, 2);  /* obj_udata */
    lua_pushnil(L);  /* EVENT_READ */
    lua_insert(L, 3);
    lua_pushnil(L);  /* EVENT_ONESHOT */
    lua_pushinteger(L, EVENT_TIMER);  /* event_flags */
    return levq_add(L);
}

/*
 * Arguments: evq_udata, pid_udata,
 *	callback (function), [timeout (milliseconds)]
 * Returns: [ev_ludata]
 */
static int
levq_add_pid (lua_State *L)
{
    lua_settop(L, 4);
    lua_pushnil(L);  /* EVENT_READ */
    lua_insert(L, 3);
    lua_pushboolean(L, 1);  /* EVENT_ONESHOT */
    lua_pushinteger(L, EVENT_PID
#ifndef _WIN32
     | EVENT_SIGNAL
#endif
    );  /* event_flags */
    return levq_add(L);
}

/*
 * Arguments: evq_udata, obj_udata, callback (function)
 * Returns: [ev_ludata]
 */
static int
levq_add_winmsg (lua_State *L)
{
    lua_settop(L, 3);
    lua_pushnil(L);  /* EVENT_READ */
    lua_insert(L, 3);
    lua_pushnil(L);  /* timeout */
    lua_pushnil(L);  /* EVENT_ONESHOT */
    lua_pushinteger(L, EVENT_WINMSG);  /* event_flags */
    return levq_add(L);
}

/*
 * Arguments: evq_udata, path (string), callback (function),
 *	[modify (boolean)]
 * Returns: [ev_ludata]
 */
static int
levq_add_dirwatch (lua_State *L)
{
    unsigned int filter = lua_toboolean(L, 4) ? EVQ_DIRWATCH_MODIFY : 0;

    lua_settop(L, 3);
    lua_pushnil(L);  /* EVENT_READ */
    lua_insert(L, 3);
    lua_pushnil(L);  /* timeout */
    lua_pushnil(L);  /* EVENT_ONESHOT */
    lua_pushinteger(L, EVENT_DIRWATCH
     | (filter << EVENT_EOF_SHIFT_RES));  /* event_flags */
    return levq_add(L);
}

/*
 * Arguments: evq_udata, signal (string), callback (function),
 *	[timeout (milliseconds), one_shot (boolean)]
 * Returns: [ev_ludata]
 */
static int
levq_add_signal (lua_State *L)
{
    const int signo = sig_flags[luaL_checkoption(L, 2, NULL, sig_names)];

    lua_settop(L, 5);
    lua_pushinteger(L, signo);  /* signal */
    lua_replace(L, 2);
    lua_pushnil(L);  /* obj_udata */
    lua_insert(L, 2);
    lua_pushinteger(L, EVENT_SIGNAL);  /* event_flags */
    return levq_add(L);
}

/*
 * Arguments: evq_udata, signal (string), ignore (boolean)
 * Returns: [evq_udata]
 */
static int
levq_ignore_signal (lua_State *L)
{
    struct event_queue *evq = checkudata(L, 1, EVQ_TYPENAME);
    const int signo = sig_flags[luaL_checkoption(L, 2, NULL, sig_names)];
    const int ignore = lua_toboolean(L, 3);

    if (!evq_ignore_signal(evq, signo, ignore)) {
	lua_settop(L, 1);
	return 1;
    }
    return sys_seterror(L, 0);
}

/*
 * Arguments: evq_udata, [signal (string)]
 * Returns: [evq_udata]
 */
static int
levq_signal (lua_State *L)
{
    struct event_queue *evq = checkudata(L, 1, EVQ_TYPENAME);
    const int signo = lua_isnoneornil(L, 2) ? EVQ_SIGEVQ
     : sig_flags[luaL_checkoption(L, 2, NULL, sig_names)];

    if (!evq_signal(evq, signo)) {
	lua_settop(L, 1);
	return 1;
    }
    return sys_seterror(L, 0);
}

/*
 * Arguments: evq_udata, sd_udata,
 *	events (string: "r", "w", "rw", "accept", "connect"),
 *	callback (function), [timeout (milliseconds), one_shot (boolean)]
 * Returns: [ev_ludata]
 */
static int
levq_add_socket (lua_State *L)
{
    const char *evstr = lua_tostring(L, 3);
    unsigned int flags = EVENT_SOCKET;

    if (evstr) {
	switch (*evstr) {
	case 'a':  /* accept */
	    flags |= EVENT_SOCKET_ACC_CONN | EVENT_READ;
	    break;
	case 'c':  /* connect */
	    flags |= EVENT_SOCKET_ACC_CONN | EVENT_WRITE | EVENT_ONESHOT;
	    break;
	}
    }
    lua_settop(L, 6);
    lua_pushinteger(L, flags);  /* event_flags */
    return levq_add(L);
}

/*
 * Arguments: evq_udata, ev_ludata, events (string: [-+] "r", "w", "rw")
 * Returns: [evq_udata]
 */
static int
levq_mod_socket (lua_State *L)
{
    struct event *ev = levq_toevent(L, 2);
    const char *evstr = luaL_checkstring(L, 3);
    int change, flags;

    lua_assert(ev && !event_deleted(ev) && (ev->flags & EVENT_SOCKET));

    change = 0;
    flags = ev->flags & (EVENT_READ | EVENT_WRITE);
    for (; *evstr; ++evstr) {
	if (*evstr == '+' || *evstr == '-')
	    change = (*evstr == '+') ? 1 : -1;
	else {
	    int rw = (*evstr == 'r') ? EVENT_READ : EVENT_WRITE;
	    switch (change) {
	    case 0:
		change = 1;
		flags &= ~(EVENT_READ | EVENT_WRITE);
	    case 1:
		flags |= rw;
		break;
	    default:
		flags &= ~rw;
	    }
	}
    }
    if (!evq_modify(ev, flags)) {
	ev->flags &= ~(EVENT_READ | EVENT_WRITE);
	ev->flags |= flags;
	lua_settop(L, 1);
	return 1;
    }
    return sys_seterror(L, 0);
}


/*
 * Arguments: evq_udata, ev_ludata, [reuse_fd (boolean)]
 * Returns: [evq_udata]
 */
static int
levq_del (lua_State *L)
{
    struct event_queue *evq = checkudata(L, 1, EVQ_TYPENAME);
    struct event *ev = levq_toevent(L, 2);
    const int reuse_fd = lua_toboolean(L, 3);
    int res = 0;

    lua_assert(ev);

#undef ARG_LAST
#define ARG_LAST	1

    lua_settop(L, ARG_LAST);
    lua_getfenv(L, 1);
    lua_rawgeti(L, ARG_LAST+1, EVQ_OBJ_UDATA);
    lua_rawgeti(L, ARG_LAST+1, EVQ_CALLBACK);

#ifdef EVQ_POST_INIT
    if (ev == evq->ev_post)
	evq->ev_post = NULL;
#endif

    if (!event_deleted(ev))
	res = evq_del(ev, reuse_fd);

    if (!(ev->flags & (EVENT_ACTIVE | EVENT_DELETE))) {
	levq_del_event(L, ARG_LAST+1, evq, ev);
    }
    ev->flags |= EVENT_DELETE;

    if (!res) {
	lua_settop(L, 1);
	return 1;
    }
    return sys_seterror(L, 0);
}

/*
 * Arguments: evq_udata, ev_ludata, [callback (function)]
 * Returns: evq_udata | callback (function)
 */
static int
levq_callback (lua_State *L)
{
    struct event *ev = levq_toevent(L, 2);
    const int top = lua_gettop(L);

    lua_assert(ev && !event_deleted(ev));

#undef ARG_LAST
#define ARG_LAST	3

    lua_settop(L, ARG_LAST);
    lua_getfenv(L, 1);
    lua_rawgeti(L, ARG_LAST+1, EVQ_CALLBACK);

    if (top < ARG_LAST) {
	lua_pop(L, 1);
	lua_rawget(L, ARG_LAST+2);
    } else {
	ev->flags &= ~(EVENT_CALLBACK | EVENT_CALLBACK_THREAD);
	if (!lua_isnoneornil(L, 3)) {
	    ev->flags |= EVENT_CALLBACK
	     | (lua_isthread(L, 3) ? EVENT_CALLBACK_THREAD : 0);
	}

	lua_pushvalue(L, 3);
	lua_rawseti(L, ARG_LAST+2, ev->ev_id);
	lua_settop(L, 1);
    }
    return 1;
}

/*
 * Arguments: evq_udata, ev_ludata, [timeout (milliseconds)]
 * Returns: [evq_udata]
 */
static int
levq_timeout (lua_State *L)
{
    struct event_queue *evq = checkudata(L, 1, EVQ_TYPENAME);
    struct event *ev = levq_toevent(L, 2);
    const msec_t timeout = lua_isnoneornil(L, 3)
     ? TIMEOUT_INFINITE : (msec_t) lua_tointeger(L, 3);

    lua_assert(ev && !event_deleted(ev) && !(ev->flags & EVENT_WINMSG));

    /* place for timeout_queue */
    if (!evq->ev_free) {
	lua_getfenv(L, 1);
	evq->ev_free = levq_new_event(L, -1, evq);
    }

    if (!evq_set_timeout(ev, timeout)) {
	lua_settop(L, 1);
	return 1;
    }
    return sys_seterror(L, 0);
}

/*
 * Arguments: evq_udata, ev_ludata, [manually/auto-reset (boolean)]
 * Returns: [evq_udata]
 */
static int
levq_timeout_manual (lua_State *L)
{
    struct event_queue *evq = checkudata(L, 1, EVQ_TYPENAME);
    struct event *ev = levq_toevent(L, 2);
    const int manual = lua_toboolean(L, 3);

    (void) evq;

    lua_assert(ev && !event_deleted(ev) && !(ev->flags & EVENT_WINMSG));

    if (manual)
	ev->flags |= EVENT_TIMEOUT_MANUAL;
    else
	ev->flags &= ~EVENT_TIMEOUT_MANUAL;

    lua_settop(L, 1);
    return 1;
}

/*
 * Arguments: evq_udata, [timeout (milliseconds), once (boolean), fetch (boolean)]
 * Returns: [evq_udata | timeout (false)]
 *	|
 * Returns: [callback (function), evq_udata, ev_ludata, obj_udata,
 *	read (boolean), write (boolean), timeout (number), eof_status (number)]
 */
static int
levq_loop (lua_State *L)
{
    struct event_queue *evq = checkudata(L, 1, EVQ_TYPENAME);
    const msec_t timeout = (lua_type(L, 2) != LUA_TNUMBER)
     ? TIMEOUT_INFINITE : (msec_t) lua_tointeger(L, 2);
    const int once = lua_toboolean(L, 3);
    const int fetch = lua_toboolean(L, 4);

#undef ARG_LAST
#define ARG_LAST	1

    lua_settop(L, ARG_LAST);
    lua_getfenv(L, 1);
    lua_rawgeti(L, ARG_LAST+1, EVQ_OBJ_UDATA);
    lua_rawgeti(L, ARG_LAST+1, EVQ_CALLBACK);

#ifdef EVQ_POST_INIT
    if (evq->ev_post) {
	evq_post_init(evq->ev_post);
	evq->ev_post = NULL;
    }
#endif

    while (!evq_is_empty(evq)) {
	struct event *ev;

	if (evq->stop) {
	    evq->stop = 0;
	    break;
	}

	if (!evq->ev_ready) {
	    const int res = evq_wait(evq, timeout);

	    if (res == EVQ_TIMEOUT) {
		lua_pushboolean(L, 0);
		return 1;
	    }
	    if (res == EVQ_FAILED)
		return sys_seterror(L, 0);
	}

	ev = evq->ev_ready;
	if (!ev) continue;
	do {
	    const unsigned int ev_flags = ev->flags;

	    ev->flags &= EVENT_MASK;  /* clear EVENT_ACTIVE and EVENT_*_RES flags */
	    evq->ev_ready = ev->next_ready;

	    if (ev_flags & EVENT_DELETE)
		levq_del_event(L, ARG_LAST+1, evq, ev);  /* postponed deletion of active event */
	    else {
		if ((ev_flags & EVENT_CALLBACK) || fetch) {
		    const int ev_id = ev->ev_id;

		    /* callback function */
		    lua_rawgeti(L, ARG_LAST+3, ev_id);
		    /* arguments */
		    lua_pushvalue(L, 1);  /* evq_udata */
		    lua_pushlightuserdata(L, ev);  /* ev_ludata */
		    lua_rawgeti(L, ARG_LAST+2, ev_id);  /* obj_udata */
		    lua_pushboolean(L, ev_flags & EVENT_READ_RES);
		    lua_pushboolean(L, ev_flags & EVENT_WRITE_RES);
		    if (ev_flags & EVENT_TIMEOUT_RES)
			lua_pushnumber(L, ev->tq->msec);
		    else
			lua_pushnil(L);
		    if (ev_flags & EVENT_EOF_MASK_RES)
			lua_pushinteger(L, (int) ev_flags >> EVENT_EOF_SHIFT_RES);
		    else
			lua_pushnil(L);
		}

		if (event_deleted(ev))
		    levq_del_event(L, ARG_LAST+1, evq, ev);  /* deletion of oneshot event */
#ifdef EVQ_POST_INIT
		else evq->ev_post = ev;
#endif

		if (fetch)
		    return 8;
		else if (!(ev_flags & EVENT_CALLBACK))
		    (void) 0;
		else if (!(ev_flags & EVENT_CALLBACK_THREAD))
		    lua_call(L, 7, 0);
		else {
		    lua_State *co = lua_tothread(L, ARG_LAST+4);

		    lua_xmove(L, co, 7);
		    lua_pop(L, 1);  /* pop coroutine */
		    switch (lua_resume(co, L, 7)) {
		    case 0:
			lua_settop(co, 0);
			if (!event_deleted(ev)) {
			    evq_del(ev, 0);
			    levq_del_event(L, ARG_LAST+1, evq, ev);
			}
			break;
		    case LUA_YIELD:
			lua_settop(co, 0);
			break;
		    default:
			lua_xmove(co, L, 1);  /* error message */
			lua_error(L);
		    }
		}

#ifdef EVQ_POST_INIT
		if (evq->ev_post) {
		    evq_post_init(evq->ev_post);
		    evq->ev_post = NULL;
		}
#endif
	    }
	    ev = evq->ev_ready;
	} while (ev);

	if (once) break;
    }

    lua_settop(L, 1);
    return 1;
}

/*
 * Arguments: evq_udata
 */
static int
levq_stop (lua_State *L)
{
    struct event_queue *evq = checkudata(L, 1, EVQ_TYPENAME);

    evq->stop = 1;
    return 0;
}

/*
 * Arguments: evq_udata, [reset (boolean)]
 * Returns: number (milliseconds)
 */
static int
levq_now (lua_State *L)
{
    struct event_queue *evq = checkudata(L, 1, EVQ_TYPENAME);
    const int reset = lua_toboolean(L, 2);

    if (reset)
	evq->now = get_milliseconds();
    lua_pushnumber(L, evq->now);
    return 1;
}

/*
 * Arguments: evq_udata, ev_ludata
 * Returns: [evq_udata]
 */
static int
levq_notify (lua_State *L)
{
    struct event_queue *evq = checkudata(L, 1, EVQ_TYPENAME);
    struct event *ev = levq_toevent(L, 2);

    lua_assert(ev && !event_deleted(ev));

    if (!(ev->flags & EVENT_TIMER))
	luaL_argerror(L, 2, "timer expected");

    ev->next_object = evq->ev_notify;
    evq->ev_notify = ev;

    if (!evq_signal(evq, EVQ_SIGEVQ)) {
	lua_settop(L, 1);
	return 1;
    }
    return sys_seterror(L, 0);
}

/*
 * Arguments: evq_udata
 * Returns: number
 */
static int
levq_size (lua_State *L)
{
    struct event_queue *evq = checkudata(L, 1, EVQ_TYPENAME);

    lua_pushinteger(L, evq->nevents);
    return 1;
}

/*
 * Arguments: evq_udata
 * Returns: string
 */
static int
levq_tostring (lua_State *L)
{
    struct event_queue *evq = checkudata(L, 1, EVQ_TYPENAME);

    lua_pushfstring(L, EVQ_TYPENAME " (%p)", evq);
    return 1;
}


#define EVQ_METHODS \
    {"event_queue",	levq_new}

static luaL_Reg evq_meth[] = {
    {"add",		levq_add},
    {"add_timer",	levq_add_timer},
    {"add_pid",		levq_add_pid},
    {"add_winmsg",	levq_add_winmsg},
    {"add_dirwatch",	levq_add_dirwatch},
    {"add_signal",	levq_add_signal},
    {"ignore_signal",	levq_ignore_signal},
    {"signal",		levq_signal},
    {"add_socket",	levq_add_socket},
    {"mod_socket",	levq_mod_socket},
    {"del",		levq_del},
    {"timeout",		levq_timeout},
    {"timeout_manual",	levq_timeout_manual},
    {"callback",	levq_callback},
    {"loop",		levq_loop},
    {"stop",		levq_stop},
    {"now",		levq_now},
    {"notify",		levq_notify},
    {"size",		levq_size},
    {"__len",		levq_size},
    {"__gc",		levq_done},
    {"__tostring",	levq_tostring},
    {NULL, NULL}
};
