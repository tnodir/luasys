/* Lua System: Event Queue */

#define EVQ_TYPENAME	"sys.evq"

#define levq_toevent(L,i) \
    (lua_type(L, (i)) == LUA_TLIGHTUSERDATA ? lua_touserdata(L, (i)) : NULL)


/*
 * Returns: [evq_udata]
 */
static int
levq_new (lua_State *L)
{
    struct event_queue *evq = lua_newuserdata(L, sizeof(struct event_queue));

    memset(evq, 0, sizeof(struct event_queue));
    evq->vmtd = sys_get_vmthread(sys_get_thread());
    evq->buf_index = EVQ_BUF_IDX;

    if (!evq_init(evq)) {
	luaL_getmetatable(L, EVQ_TYPENAME);
	lua_setmetatable(L, -2);

	lua_newtable(L);  /* environ. | {ev_ludata => get_trigger_func} */
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

    lua_getfenv(L, 1);
    lua_rawgeti(L, -1, EVQ_OBJ_UDATA);

    /* delete object events */
    lua_pushnil(L);
    while (lua_next(L, -2)) {
	struct event *ev = lua_touserdata(L, -2);

	if (ev && !event_deleted(ev))
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
    }
    else {
	const int n = evq->buf_nevents;
	const int i = evq->buf_index;
	const int nmax = (1 << i);

	lua_rawgeti(L, idx, i);
	ev = lua_touserdata(L, -1);
	lua_pop(L, 1);
	if (ev) {
	    ev += n;
	    if (++evq->buf_nevents >= nmax) {
		evq->buf_nevents = 0;
		evq->buf_index++;
	    }
	}
	else {
	    ev = lua_newuserdata(L, nmax * sizeof(struct event));
	    lua_rawseti(L, idx, i);
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
 *	event_flags (number), get_trigger_func (cfunction)]
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
    sys_get_trigger_t get_trigger = ev_flags
     ? (sys_get_trigger_t) lua_tocfunction(L, 8) : NULL;
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
	if (ev_flags & EVENT_OBJECT) {
	    struct sys_thread *vmtd;
	    struct event **ev_head;

	    lua_pushvalue(L, 2);
	    ev_head = (void *) get_trigger(L, &vmtd);
	    if (!ev_head) goto err;
	    lua_pop(L, 1);

	    if (vmtd != evq->vmtd) sys_vm2_enter(vmtd);
	    ev->next_object = *ev_head;
	    *ev_head = ev;
	    if (vmtd != evq->vmtd) sys_vm2_leave(vmtd);
	}

	res = evq_add_timer(evq, ev, timeout);
    }
    else {
	if (ev_flags & EVENT_DIRWATCH) {
	    const char *path = luaL_checkstring(L, 2);
	    res = evq_add_dirwatch(evq, ev, path);
	}
	else
	    res = evq_add(evq, ev);

	if (!res && timeout != TIMEOUT_INFINITE) {
	    evq_set_timeout(ev, timeout);
	}
    }
    if (!res) {
	const int ev_id = ev->ev_id;

	lua_pushlightuserdata(L, ev);

	/* get_trigger_func */
	if (get_trigger) {
	    lua_pushvalue(L, -1);
	    lua_pushcfunction(L, (lua_CFunction) get_trigger);
	    lua_rawset(L, ARG_LAST+1);
	}
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
 err:
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
 * Arguments: evq_udata, object (any), [metatable (table)],
 *	events (string: "r", "w", "rw"),
 *	callback (function), [timeout (milliseconds), one_shot (boolean)]
 * Returns: [ev_ludata]
 */
static int
levq_add_trigger (lua_State *L)
{
    int tblidx;

    lua_settop(L, 7);
    if (lua_istable(L, 3))
	tblidx = 3;
    else {
	lua_pop(L, 1);  /* pop nil */
	lua_getmetatable(L, 2);
	tblidx = 7;
    }
    lua_pushinteger(L, EVENT_TIMER | EVENT_OBJECT);  /* event_flags */

    lua_getfield(L, tblidx, SYS_TRIGGER_TAG);
    lua_remove(L, tblidx);
    luaL_checktype(L, 8, LUA_TFUNCTION);

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

    if (!evq_ignore_signal(evq, signo, lua_toboolean(L, 3))) {
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

    if (!ev || event_deleted(ev) || !(ev->flags & EVENT_SOCKET))
	return 0;

    change = 0;
    flags = ev->flags & (EVENT_READ | EVENT_WRITE);
    for (; *evstr; ++evstr) {
	if (*evstr == '+' || *evstr == '-')
	    change = (*evstr++ == '+') ? 1 : -1;
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
    unsigned int ev_flags;
    int res = 0;

    if (!ev) return 0;

#undef ARG_LAST
#define ARG_LAST	1

    lua_settop(L, ARG_LAST);
    lua_getfenv(L, 1);
    lua_rawgeti(L, ARG_LAST+1, EVQ_OBJ_UDATA);
    lua_rawgeti(L, ARG_LAST+1, EVQ_CALLBACK);

    ev_flags = ev->flags;

    if (!event_deleted(ev)) {
	if (ev_flags & EVENT_OBJECT) {
	    sys_get_trigger_t get_trigger;
	    struct sys_thread *vmtd;
	    struct event **ev_head;

	    lua_pushlightuserdata(L, ev);
	    /* get_trigger_func */
	    lua_pushvalue(L, -1);
	    lua_rawget(L, ARG_LAST+1);
	    get_trigger = (sys_get_trigger_t) lua_tocfunction(L, -1);
	    lua_pop(L, 1);
	    /* clear the function */
	    lua_pushnil(L);
	    lua_rawset(L, ARG_LAST+1);

	    lua_rawgeti(L, ARG_LAST+2, ev->ev_id);
	    ev_head = (void *) get_trigger(L, &vmtd);
	    lua_pop(L, 1);

	    if (vmtd != evq->vmtd) sys_vm2_enter(vmtd);
	    if (ev == *ev_head)
		*ev_head = ev->next_object;
	    else {
		struct event *virt = *ev_head;
		while (virt->next_object != ev)
		    virt = virt->next_object;
		virt->next_object = ev->next_object;
	    }
	    if (vmtd != evq->vmtd) sys_vm2_leave(vmtd);
	}

	res = evq_del(ev, reuse_fd);
    }

    if (!(ev_flags & (EVENT_ACTIVE | EVENT_DELETE))) {
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

    if (!ev) return 0;

#undef ARG_LAST
#define ARG_LAST	3

    lua_settop(L, ARG_LAST);
    lua_getfenv(L, 1);
    lua_rawgeti(L, ARG_LAST+1, EVQ_CALLBACK);

    if (top < ARG_LAST) {
	lua_pop(L, 1);
	lua_rawget(L, ARG_LAST+2);
    }
    else {
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
    msec_t timeout = lua_isnoneornil(L, 3)
     ? TIMEOUT_INFINITE : (msec_t) lua_tointeger(L, 3);

    if (!ev || event_deleted(ev) || (ev->flags & EVENT_WINMSG))
	return 0;

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
 * Arguments: evq_udata, [callback (function)]
 */
static int
levq_on_interrupt (lua_State *L)
{
    lua_settop(L, 2);
    lua_getfenv(L, 1);
    lua_pushvalue(L, 2);
    lua_rawseti(L, -2, EVQ_ON_INTR);
    return 0;
}

/*
 * Arguments: evq_udata, [timeout (milliseconds), once (boolean)]
 * Returns: [evq_udata]
 */
static int
levq_loop (lua_State *L)
{
    struct event_queue *evq = checkudata(L, 1, EVQ_TYPENAME);
    const msec_t timeout = (lua_type(L, 2) != LUA_TNUMBER)
     ? TIMEOUT_INFINITE : (msec_t) lua_tointeger(L, 2);
    const int is_once = lua_isboolean(L, -1) && lua_toboolean(L, -1);

#undef ARG_LAST
#define ARG_LAST	1

    lua_settop(L, ARG_LAST);
    lua_getfenv(L, 1);
    lua_rawgeti(L, ARG_LAST+1, EVQ_OBJ_UDATA);
    lua_rawgeti(L, ARG_LAST+1, EVQ_CALLBACK);

    while (!evq_is_empty(evq)) {
	struct event *ev, *ev_next;

	if (evq->stop) {
	    evq->stop = 0;
	    break;
	}

	if (!evq->ev_ready) {
	    const int res = evq_wait(evq, timeout);

	    if (res == EVQ_TIMEOUT)
		break;
	    if (res == EVQ_FAILED)
		return sys_seterror(L, 0);
	}

	if (evq->intr) {
	    evq->intr = 0;
	    lua_rawgeti(L, ARG_LAST+1, EVQ_ON_INTR);
	    if (lua_isfunction(L, -1)) {
		lua_pushvalue(L, 1);  /* evq_udata */
		lua_call(L, 1, 0);
	    }
	}

	if (is_once && evq->ev_ready) {
	    evq->stop = 1;
	}

	for (ev = evq->ev_ready; ev; ev = ev_next) {
	    const unsigned int ev_flags = ev->flags;

	    evq->ev_ready = ev_next = ev->next_ready;

	    if (!(ev_flags & EVENT_DELETE)) {
		if (ev_flags & EVENT_CALLBACK) {
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

		    if (!(ev_flags & EVENT_CALLBACK_THREAD))
			lua_call(L, 7, 0);
		    else {
			lua_State *co = lua_tothread(L, ARG_LAST+4);
			int status;

			lua_xmove(L, co, 7);
			lua_pop(L, 1);  /* pop coroutine */
			status = lua_resume(co, 7);
			if (status == 0 || status == LUA_YIELD)
			    lua_settop(co, 0);
			else {
			    lua_xmove(co, L, 1);  /* error message */
			    lua_error(L);
			}
		    }
		}
		ev->flags &= EVENT_MASK;  /* clear EVENT_ACTIVE and EVENT_*_RES flags */
	    }
	    /* delete if called {evq_del | EVENT_ONESHOT} */
	    if (event_deleted(ev))
		levq_del_event(L, ARG_LAST+1, evq, ev);
	    else
		evq_post_call(ev, ev_flags);
	}
    }

    lua_settop(L, 1);
    return 1;
}

/*
 * Arguments: evq_udata
 * Returns: [evq_udata]
 */
static int
levq_interrupt (lua_State *L)
{
    struct event_queue *evq = checkudata(L, 1, EVQ_TYPENAME);

    evq->intr = 1;
    lua_settop(L, evq_interrupt(evq) ? 0 : 1);
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

int
sys_trigger_notify (sys_trigger_t *trigger, int flags)
{
    struct sys_thread *vmtd = sys_get_vmthread(sys_get_thread());
    struct event *ev = (struct event *) *trigger;
    struct event_queue *evq = event_get_evq(ev);
    struct event *ev_ready;
    const int deleted = (flags & SYS_EVDEL) ? EVENT_DELETE : 0;

    if (deleted) *trigger = NULL;

    if (vmtd != evq->vmtd) sys_vm2_enter(evq->vmtd);
    ev_ready = evq->ev_ready;

    do {
	const unsigned int ev_flags = ev->flags;
	int res = 0;

	if ((flags & SYS_EVREAD) && (ev_flags & EVENT_READ))
	    res = EVENT_READ_RES;
	if ((flags & SYS_EVWRITE) && (ev_flags & EVENT_WRITE))
	    res |= EVENT_WRITE_RES;
	if (flags & SYS_EVEOF)
	    res |= EVENT_EOF_RES;

	if (res || deleted) {
	    struct event_queue *cur_evq = event_get_evq(ev);

	    ev->flags |= (res ? res : deleted);
	    if (ev_flags & EVENT_ACTIVE)
		continue;

	    ev->flags |= EVENT_ACTIVE;
	    if (deleted || (ev_flags & EVENT_ONESHOT))
		evq_del(ev, 0);
	    else if (ev->tq) {
		evq_set_timeout(ev, ev->tq->msec);  /* timeout_reset */
	    }

	    /* Is the event from the same event_queue? */
	    if (evq != cur_evq) {
		evq->ev_ready = ev_ready;
		if (vmtd != evq->vmtd) sys_vm2_leave(evq->vmtd);

		evq_interrupt(evq);

		evq = cur_evq;
		if (vmtd != evq->vmtd) sys_vm2_enter(evq->vmtd);
		ev_ready = evq->ev_ready;
	    }

	    ev->next_ready = ev_ready;
	    ev_ready = ev;
	}
	ev = ev->next_object;
    } while (ev);

    evq->ev_ready = ev_ready;
    if (vmtd != evq->vmtd) sys_vm2_leave(evq->vmtd);

    return evq_interrupt(evq);
}

/*
 * Arguments: evq_udata, ev_ludata, [events (string: "r", "w", "rw")]
 * Returns: [evq_udata]
 */
static int
levq_notify (lua_State *L)
{
    struct event_queue *evq = checkudata(L, 1, EVQ_TYPENAME);
    struct event *ev = levq_toevent(L, 2);
    const char *evstr = lua_tostring(L, 3);

    if (!ev || event_deleted(ev) || !(ev->flags & EVENT_TIMER))
	return 0;

    ev->flags |= EVENT_ACTIVE
     | (!evstr ? EVENT_READ_RES : (evstr[0] == 'r')
     ? EVENT_READ_RES | (evstr[1] ? EVENT_WRITE_RES : 0) : EVENT_WRITE_RES);

    if (ev->tq) {
	if (ev->flags & EVENT_ONESHOT)
	    evq_del(ev, 0);
	else
	    evq_set_timeout(ev, ev->tq->msec);  /* timeout_reset */
    }

    ev->next_ready = evq->ev_ready;
    evq->ev_ready = ev;

    if (!evq_interrupt(evq)) {
	lua_settop(L, 1);
	return 1;
    }
    return sys_seterror(L, 0);
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

static luaL_reg evq_meth[] = {
    {"add",		levq_add},
    {"add_timer",	levq_add_timer},
    {"add_pid",		levq_add_pid},
    {"add_winmsg",	levq_add_winmsg},
    {"add_dirwatch",	levq_add_dirwatch},
    {"add_trigger",	levq_add_trigger},
    {"add_signal",	levq_add_signal},
    {"ignore_signal",	levq_ignore_signal},
    {"add_socket",	levq_add_socket},
    {"mod_socket",	levq_mod_socket},
    {"del",		levq_del},
    {"timeout",		levq_timeout},
    {"callback",	levq_callback},
    {"on_interrupt",	levq_on_interrupt},
    {"loop",		levq_loop},
    {"interrupt",	levq_interrupt},
    {"stop",		levq_stop},
    {"now",		levq_now},
    {"notify",		levq_notify},
    {"__gc",		levq_done},
    {"__tostring",	levq_tostring},
    {NULL, NULL}
};
