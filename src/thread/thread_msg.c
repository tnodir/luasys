/* Lua System: Threading: VM-Threads Communication */

#define MSG_MAXSIZE		512

#define MSG_BUFF_INITIALSIZE	8 * MSG_MAXSIZE

struct message_item {
    int type: 8;  /* lua type */
    int len: 16;  /* length of value in bytes */
    union {
	lua_Number num;
	char bool;
	void *ptr;
	char str[1];
    } v;
};

struct message {
    struct sys_thread *src_td;  /* source */
    int size;  /* size of message in bytes */
    char items[MSG_MAXSIZE];  /* array of message items */
};


/*
 * Arguments: thread_ludata, message_items (any) ...
 */
static void
thread_msg_build (lua_State *L, struct message *msg)
{
    char *cp = msg->items;
    char *endp = cp + MSG_MAXSIZE;
    int i, top = lua_gettop(L);

    for (i = 2; i <= top; ++i) {
	struct message_item *item = (struct message_item *) cp;
	const int type = lua_type(L, i);
	const char *s = NULL;
	size_t len = sizeof(item->v);

	cp += sizeof(struct message_item) - sizeof(item->v);
	if (type == LUA_TSTRING)
	    s = lua_tolstring(L, i, &len);

	if (cp + len >= endp)
	    luaL_argerror(L, i, "message is too big");

	switch (type) {
	case LUA_TSTRING:
	    memcpy(&item->v, s, len);
	    break;
	case LUA_TNUMBER:
	    item->v.num = lua_tonumber(L, i);
	    len = sizeof(item->v.num);
	    break;
	case LUA_TBOOLEAN:
	    item->v.bool = (char) lua_toboolean(L, i);
	    len = sizeof(item->v.bool);
	    break;
	case LUA_TNIL:
	    len = 0;
	    break;
	case LUA_TLIGHTUSERDATA:
	case LUA_TUSERDATA:
	    item->v.ptr = lua_touserdata(L, i);
	    len = sizeof(item->v.ptr);
	    break;
	default:
	    luaL_argerror(L, i, "primitive type expected");
	}
	item->type = type;
	item->len = len;
	cp += len;
    }
    msg->size = (sizeof(struct message) - MSG_MAXSIZE) + cp - msg->items;
}

/*
 * Returns: thread_ludata, [message_items (any) ...]
 */
static int
thread_msg_parse (lua_State *L, struct message *msg)
{
    char *cp = msg->items;
    char *endp = (char *) msg + msg->size;
    int i;

    lua_pushlightuserdata(L, msg->src_td);

    for (i = 1; cp < endp; ++i) {
	struct message_item *item = (struct message_item *) cp;
	const int len = item->len;

	switch (item->type) {
	case LUA_TSTRING:
	    lua_pushlstring(L, (char *) &item->v, len);
	    break;
	case LUA_TNUMBER:
	    lua_pushnumber(L, item->v.num);
	    break;
	case LUA_TBOOLEAN:
	    lua_pushboolean(L, item->v.bool);
	    break;
	case LUA_TNIL:
	    lua_pushnil(L);
	    break;
	default:
	    lua_pushlightuserdata(L, item->v.ptr);
	}
	cp += sizeof(struct message_item) - sizeof(item->v) + len;
    }
    return i;
}


/*
 * Arguments: thread_ludata, [message_items (any) ...]
 * Returns: [thread_ludata]
 */
static int
thread_msg_send (lua_State *L)
{
    struct sys_vmthread *vmtd = lua_touserdata(L, 1);
    struct message msg;
    thread_critsect_t *csp;

    if (!vmtd) luaL_argerror(L, 1, "thread id. expected");

    msg.src_td = sys_get_thread();
    if (!msg.src_td) luaL_argerror(L, 0, "Threading not initialized");

    /* construct the message */
    thread_msg_build(L, &msg);

    vmtd = vmtd->td.vmtd;

#ifndef _WIN32
    csp = &vmtd->bufev.cs;
#else
    csp = &vmtd->bufcs;
#endif

    /* copy the message */
    thread_critsect_enter(csp);
    {
	struct thread_msg_buf buf = vmtd->buffer;
	int nreq = buf.top + msg.size - buf.len;  /* additional required space */

	if (nreq > 0) {
	    if (buf.idx >= nreq) {
		memmove(buf.ptr, buf.ptr + buf.idx, buf.top - buf.idx);
		buf.top = buf.idx;
		buf.idx = 0;
	    } else {
		const int newlen = buf.len ? 2 * buf.len : MSG_BUFF_INITIALSIZE;
		void *p = realloc(buf.ptr, newlen);

		if (!p) {
		    thread_critsect_leave(csp);
		    return 0;
		}
		buf.ptr = p;
		buf.len = newlen;
	    }
	}

	memcpy(buf.ptr + buf.top, &msg, msg.size);
	buf.top += msg.size;
	buf.nmsg++;
	vmtd->buffer = buf;

	/* notify event_queue */
	if (buf.nmsg == 1 && vmtd->td.trigger)
	    sys_trigger_notify(&vmtd->td.trigger, SYS_EVREAD);

	thread_event_signal_nolock(&vmtd->bufev);
    }
    thread_critsect_leave(csp);

    lua_settop(L, 1);
    return 1;
}

/*
 * Arguments: [thread_ludata, timeout (milliseconds)]
 * Returns: [thread_ludata, message_items (any) ... | timedout (false)]
 */
static int
thread_msg_recv (lua_State *L)
{
    struct sys_vmthread *vmtd = lua_isuserdata(L, 1)
     ? lua_touserdata(L, 1) : (struct sys_vmthread *) sys_get_thread();
    const msec_t timeout = !lua_isnumber(L, -1)
     ? TIMEOUT_INFINITE : (msec_t) lua_tointeger(L, -1);
    thread_critsect_t *csp;

    if (!vmtd) luaL_argerror(L, 0, "Threading not initialized");

    vmtd = vmtd->td.vmtd;

#ifndef _WIN32
    csp = &vmtd->bufev.cs;
#else
    csp = &vmtd->bufcs;
#endif

    for (; ; ) {
	struct message msg;
	int res = 0;

	thread_critsect_enter(csp);
	if (vmtd->buffer.nmsg) {
	    struct thread_msg_buf buf = vmtd->buffer;
	    struct message *mp = (struct message *) (buf.ptr + buf.idx);

	    memcpy(&msg, mp, mp->size);
	    buf.idx += mp->size;
	    if (buf.idx == buf.top)
		buf.idx = buf.top = 0;
	    buf.nmsg--;
	    vmtd->buffer = buf;
	    res = 1;
	}
	thread_critsect_leave(csp);

	if (res) return thread_msg_parse(L, &msg);

	/* wait signal */
	res = thread_event_wait(&vmtd->bufev, timeout);
	if (res) {
	    if (res == 1) {
		lua_pushboolean(L, 0);
		return 1;  /* timed out */
	    }
	    return sys_seterror(L, 0);
	}
    }
}

/*
 * Arguments: [thread_ludata]
 * Returns: [number]
 */
static int
thread_msg_count (lua_State *L)
{
    struct sys_vmthread *vmtd = lua_isuserdata(L, 1)
     ? lua_touserdata(L, 1) : (struct sys_vmthread *) sys_get_thread();
    thread_critsect_t *csp;
    int nmsg;

    if (!vmtd) return 0;
    vmtd = vmtd->td.vmtd;

#ifndef _WIN32
    csp = &vmtd->bufev.cs;
#else
    csp = &vmtd->bufcs;
#endif

    thread_critsect_enter(csp);
    nmsg = vmtd->buffer.nmsg;
    thread_critsect_leave(csp);

    lua_pushinteger(L, nmsg);
    return 1;
}

