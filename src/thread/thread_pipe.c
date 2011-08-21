/* Lua System: Threading: Pipes (VM-Threads IPC) */

#define PIPE_TYPENAME	"sys.thread.pipe"

#define MSG_MAXSIZE		512
#define MSG_BUFF_INITIALSIZE	(8 * MSG_MAXSIZE)
#define MSG_ITEM_ALIGN		4

struct pipe_buf {
    char *ptr;
    int len;
    int idx, top;  /* buffer indexes */
    int nmsg;  /* number of messages */
};

struct pipe {
#ifdef _WIN32
    thread_critsect_t bufcs;  /* guard access to buffer */
#endif
    thread_event_t bufev;
    struct pipe_buf volatile buffer;
    int nref;
};

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
    int size: 16;  /* size of message in bytes */
    int nitems: 16;  /* number of message items */
    char items[MSG_MAXSIZE];  /* array of message items */
};

#ifndef _WIN32
#define pipe_buf_csp(pp)	(&pp->bufev.cs)
#else
#define pipe_buf_csp(pp)	(&pp->bufcs)
#endif


/*
 * Returns: [pipe_udata]
 */
static int
pipe_new (lua_State *L)
{
    struct pipe *pp;
    struct pipe **ppp = (struct pipe **) lua_newuserdata(L, sizeof(void *));

    *ppp = NULL;
    luaL_getmetatable(L, PIPE_TYPENAME);
    lua_setmetatable(L, -2);

    pp = calloc(sizeof(struct pipe), 1);
    if (!pp) goto err;

    if (thread_event_new(&pp->bufev))
	goto err_clean;
#ifdef _WIN32
    if (thread_critsect_new(&pp->bufcs)) {
	thread_event_del(&pp->bufev);
	goto err_clean;
    }
#endif
    *ppp = pp;
    return 1;
 err_clean:
    free(pp);
 err:
    return sys_seterror(L, 0);
}

/*
 * Returns: pipe_udata, dest. thread (ludata)
 */
static int
pipe_xdup (lua_State *L)
{
    struct pipe *pp = lua_unboxpointer(L, 1, PIPE_TYPENAME);
    lua_State *L2 = (lua_State *) lua_touserdata(L, 2);
    thread_critsect_t *csp = pipe_buf_csp(pp);

    if (!L2) luaL_argerror(L, 2, "VM-Thread expected");

    lua_boxpointer(L2, pp);
    luaL_getmetatable(L2, PIPE_TYPENAME);
    lua_setmetatable(L2, -2);

    thread_critsect_enter(csp);
    pp->nref++;
    thread_critsect_leave(csp);
    return 0;
}

/*
 * Arguments: pipe_udata
 */
static int
pipe_close (lua_State *L)
{
    struct pipe **ppp = (struct pipe **) checkudata(L, 1, PIPE_TYPENAME);

    if (*ppp) {
	struct pipe *pp = *ppp;
	thread_critsect_t *csp = pipe_buf_csp(pp);
	int nref;

	thread_critsect_enter(csp);
	nref = pp->nref--;
	thread_critsect_leave(csp);

	if (!nref) {
	    thread_event_del(&pp->bufev);
#ifdef _WIN32
	    thread_critsect_del(&pp->bufcs);
#endif
	    free(pp->buffer.ptr);
	    free(pp);
	}
	*ppp = NULL;
    }
    return 0;
}

/*
 * Arguments: ..., message_items (any) ...
 */
static void
pipe_msg_build (lua_State *L, struct message *msg, int idx)
{
    char *cp = msg->items;
    char *endp = cp + MSG_MAXSIZE - MSG_ITEM_ALIGN;
    int top = lua_gettop(L);

    for (; idx <= top; ++idx) {
	struct message_item *item = (struct message_item *) cp;
	const int type = lua_type(L, idx);
	const char *s = NULL;
	size_t len = sizeof(item->v);

	cp += offsetof(struct message_item, v);
	if (type == LUA_TSTRING)
	    s = lua_tolstring(L, idx, &len);

	if (cp + len >= endp)
	    luaL_argerror(L, idx, "message is too big");

	switch (type) {
	case LUA_TSTRING:
	    memcpy(&item->v, s, len);
	    break;
	case LUA_TNUMBER:
	    item->v.num = lua_tonumber(L, idx);
	    len = sizeof(item->v.num);
	    break;
	case LUA_TBOOLEAN:
	    item->v.bool = (char) lua_toboolean(L, idx);
	    len = sizeof(item->v.bool);
	    break;
	case LUA_TNIL:
	    len = 0;
	    break;
	case LUA_TLIGHTUSERDATA:
	case LUA_TUSERDATA:
	    item->v.ptr = lua_touserdata(L, idx);
	    len = sizeof(item->v.ptr);
	    break;
	default:
	    luaL_argerror(L, idx, "primitive type expected");
	}
	item->type = type;
	item->len = len;
	cp += len;
	cp += (len & (MSG_ITEM_ALIGN-1)) ? MSG_ITEM_ALIGN - (len & (MSG_ITEM_ALIGN-1)) : 0;
    }
    msg->nitems = top;
    msg->size = offsetof(struct message, items) + cp - msg->items;
}

/*
 * Returns: message_items (any) ...
 */
static int
pipe_msg_parse (lua_State *L, struct message *msg)
{
    char *cp = msg->items;
    char *endp = (char *) msg + msg->size;
    int i;

    luaL_checkstack(L, msg->nitems, "too large message");

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
	cp += offsetof(struct message_item, v) + len;
	cp += (len & (MSG_ITEM_ALIGN-1)) ? MSG_ITEM_ALIGN - (len & (MSG_ITEM_ALIGN-1)) : 0;
    }
    return i - 1;
}


/*
 * Arguments: pipe_udata, message_items (any) ...
 * Returns: [pipe_udata]
 */
static int
pipe_put (lua_State *L)
{
    struct pipe *pp = lua_unboxpointer(L, 1, PIPE_TYPENAME);
    struct message msg;
    thread_critsect_t *csp = pipe_buf_csp(pp);

    pipe_msg_build(L, &msg, 2);  /* construct the message */

    /* copy the message */
    thread_critsect_enter(csp);
    {
	struct pipe_buf buf = pp->buffer;
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
	pp->buffer = buf;

	if (buf.nmsg == 1) {
	    thread_event_signal_nolock(&pp->bufev);
	}
    }
    thread_critsect_leave(csp);

    lua_settop(L, 1);
    return 1;
}

/*
 * Arguments: pipe_udata, [timeout (milliseconds)]
 * Returns: [message_items (any) ... | timedout (false)]
 */
static int
pipe_get (lua_State *L)
{
    struct pipe *pp = lua_unboxpointer(L, 1, PIPE_TYPENAME);
    const msec_t timeout = !lua_isnumber(L, -1)
     ? TIMEOUT_INFINITE : (msec_t) lua_tointeger(L, -1);
    thread_critsect_t *csp = pipe_buf_csp(pp);

    for (; ; ) {
	struct message msg;
	int res = 0;

	thread_critsect_enter(csp);
	if (pp->buffer.nmsg) {
	    struct pipe_buf buf = pp->buffer;
	    struct message *mp = (struct message *) (buf.ptr + buf.idx);

	    memcpy(&msg, mp, mp->size);
	    buf.idx += mp->size;
	    if (buf.idx == buf.top)
		buf.idx = buf.top = 0;
	    buf.nmsg--;
	    pp->buffer = buf;
	    res = 1;
	}
	thread_critsect_leave(csp);

	if (res) return pipe_msg_parse(L, &msg);

	/* wait signal */
	res = thread_event_wait(&pp->bufev, timeout);
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
 * Arguments: pipe_udata, [timeout (milliseconds)]
 * Returns: [signalled/timedout (boolean)]
 */
static int
pipe_wait (lua_State *L)
{
    struct pipe *pp = lua_unboxpointer(L, 1, PIPE_TYPENAME);
    const msec_t timeout = lua_isnoneornil(L, 2)
     ? TIMEOUT_INFINITE : (msec_t) lua_tointeger(L, 2);
    int res;

    res = thread_event_wait(&pp->bufev, timeout);
    if (res >= 0) {
	lua_pushboolean(L, !res);
	return 1;
    }
    return sys_seterror(L, 0);
}

/*
 * Arguments: pipe_udata
 * Returns: [number]
 */
static int
pipe_count (lua_State *L)
{
    struct pipe *pp = lua_unboxpointer(L, 1, PIPE_TYPENAME);
    thread_critsect_t *csp = pipe_buf_csp(pp);
    int nmsg;

    thread_critsect_enter(csp);
    nmsg = pp->buffer.nmsg;
    thread_critsect_leave(csp);

    lua_pushinteger(L, nmsg);
    return 1;
}

/*
 * Arguments: pipe_udata
 * Returns: string
 */
static int
pipe_tostring (lua_State *L)
{
    struct pipe *pp = lua_unboxpointer(L, 1, PIPE_TYPENAME);

    lua_pushfstring(L, PIPE_TYPENAME " (%p)", &pp->bufev);
    return 1;
}


static luaL_Reg pipe_meth[] = {
    {THREAD_XDUP_TAG,	pipe_xdup},
    {"put",		pipe_put},
    {"get",		pipe_get},
    {"wait",		pipe_wait},
    {"__len",		pipe_count},
    {"__tostring",	pipe_tostring},
    {"__gc",		pipe_close},
    {NULL, NULL}
};
