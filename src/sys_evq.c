/* Lua System: Event Queue */

/* Thread-safe synchronous operations */
struct evq_sync_op {
  struct evq_sync_op *next;
  lua_State *L;
  int fn_idx:		16;  /* index of function */
  int status:		1;  /* result status */
  int is_sched_add:	1;  /* called from scheduler? */
  struct sys_thread *td;  /* called thread */
};

#define EVQ_APP_EXTRA \
  struct evq_sync_op * volatile sync_op; \
  lua_State *L;  /* storage */ \
  thread_event_t wait_tev;  /* evq_wait synchronization */ \
  unsigned int volatile nidles;  /* number of idle loops */ \
  unsigned int volatile nactives;  /* number of active event handlers */


#include "event/evq.c"


/* Event Queue environ. table reserved indexes */
#define EVQ_ENV_BUF_IDX		6  /* initial buffer index */
#define EVQ_ENV_BUF_MAX		24  /* maximum buffer index */

/* Event Queue coroutine reserved indexes */
#define EVQ_CORO_ENV		1  /* environ. */
#define EVQ_CORO_CALLBACK	2  /* table: callback functions */
#define EVQ_CORO_UDATA		3  /* table: event objects */
#define EVQ_CORO_TQ		4  /* table: timeout queues */

#define levq_toevent(L,i) \
    (lua_type(L, (i)) == LUA_TLIGHTUSERDATA \
     ? lua_touserdata(L, (i)) : NULL)

#define levq_free_event(evq,ev) \
  do { \
    ev->next_ready = evq->ev_free; \
    evq->ev_free = ev; \
  } while (0)


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


static void
levq_control_wait (struct event_queue *evq, const int stop)
{
  if (stop) {
    if (!evq->nactives++ && (evq->flags & EVQ_FLAG_WAITING)) {
      struct sys_thread *td = sys_thread_get();

      evq_signal(evq, EVQ_SIGEVQ);
      do sys_thread_switch(td);
      while (evq->flags & EVQ_FLAG_WAITING);
    }
  } else {
    if (!--evq->nactives && evq->nidles)
      thread_event_signal(&evq->wait_tev);
  }
}

static struct timeout_queue *
levq_timeout_map (struct event_queue *evq, struct timeout_queue *tq,
                  const msec_t msec, const int is_remove)
{
  lua_State *L = evq->L;

  if (tq) {
    if (is_remove)
      lua_pushnil(L);  /* remove existing entry */
    else
      lua_pushlightuserdata(L, tq);  /* add new entry */
    lua_rawseti(L, EVQ_CORO_TQ, msec);
  } else {  /* search the entry */
    lua_rawgeti(L, EVQ_CORO_TQ, msec);
    tq = lua_touserdata(L, -1);
    lua_pop(L, 1);
  }
  return tq;
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
  lua_assert(sizeof(struct event) >= sizeof(struct evq_sync_op));

  if (!evq_init(evq)) {
    lua_State *NL;

    luaL_getmetatable(L, EVQ_TYPENAME);
    lua_setmetatable(L, -2);

    lua_newtable(L);  /* environ. (EVQ_CORO_ENV) */
    lua_pushvalue(L, -1);
    lua_setfenv(L, -3);

    NL = lua_newthread(L);
    if (!NL) return 0;

    if (thread_event_new(&evq->wait_tev))
      goto err;

    evq->tq_map_fn = levq_timeout_map;
    evq->L = NL;
    lua_rawsetp(L, -2, NL);  /* save coroutine to avoid GC */

    lua_newtable(L);  /* {ev_id => cb_func} (EVQ_CORO_CALLBACK) */
    lua_newtable(L);  /* {ev_id => obj_udata} (EVQ_CORO_UDATA) */
    lua_newtable(L);  /* {msec => tq_ludata} (EVQ_CORO_TQ) */
    lua_xmove(L, NL, 4);
    return 1;
  }
 err:
  return sys_seterror(L, 0);
}

/*
 * Arguments: evq_udata
 */
static int
levq_done (lua_State *L)
{
  struct event_queue *evq = checkudata(L, 1, EVQ_TYPENAME);
  struct event *buffers[EVQ_ENV_BUF_MAX + 1];  /* cache */
  lua_State *NL = evq->L;

  if (!NL) return 0;

  memset(buffers, 0, sizeof(buffers));

  /* delete object events */
  lua_pushnil(NL);
  while (lua_next(NL, EVQ_CORO_UDATA)) {
    const int ev_id = lua_tointeger(NL, -2);
    const int buf_idx = getmaxbit(
     (ev_id | ((1 << EVQ_ENV_BUF_IDX) - 1)) + 1);
    const int nmax = (1 << buf_idx);
    struct event *ev = buffers[buf_idx];

    if (!ev) {
      lua_rawgeti(NL, EVQ_CORO_ENV, buf_idx - EVQ_ENV_BUF_IDX + 1);
      ev = lua_touserdata(NL, -1);
      lua_pop(NL, 1);  /* pop events buffer */
      buffers[buf_idx] = ev;
    }
    ev += ev_id - ((nmax - 1) & ~((1 << EVQ_ENV_BUF_IDX) - 1));

    if (!event_deleted(ev))
      evq_del(ev, 0);
    lua_pop(NL, 1);  /* pop value */
  }

  (void) thread_event_del(&evq->wait_tev);

  evq_done(evq);
  evq->L = NULL;
  return 0;
}


static struct event *
levq_new_event (struct event_queue *evq)
{
  struct event *ev;
  int ev_id;

  ev = evq->ev_free;
  if (ev) {
    evq->ev_free = ev->next_ready;
    ev_id = ev->ev_id;
  } else {
    lua_State *L = evq->L;
    const int n = evq->buf_nevents;
    const int buf_idx = evq->buf_index + EVQ_ENV_BUF_IDX;
    const int nmax = (1 << buf_idx);

    lua_rawgeti(L, EVQ_CORO_ENV, buf_idx - EVQ_ENV_BUF_IDX + 1);
    ev = lua_touserdata(L, -1);
    lua_pop(L, 1);
    if (ev) {
      ev += n;
      if (++evq->buf_nevents >= nmax) {
        evq->buf_nevents = 0;
        evq->buf_index++;
      }
    } else {
      if (buf_idx > EVQ_ENV_BUF_MAX)
        luaL_argerror(L, 1, "too many events");
      ev = lua_newuserdata(L, nmax * sizeof(struct event));
      lua_rawseti(L, EVQ_CORO_ENV, buf_idx - EVQ_ENV_BUF_IDX + 1);
      evq->buf_nevents = 1;
    }
    ev_id = n + ((nmax - 1) & ~((1 << EVQ_ENV_BUF_IDX) - 1));
  }
  memset(ev, 0, sizeof(struct event));
  ev->ev_id = ev_id;
  return ev;
}

static void
levq_del_event (struct event_queue *evq, struct event *ev)
{
  lua_State *L = evq->L;
  const int ev_id = ev->ev_id;

  /* cb_fun */
  if (ev->flags & EVENT_CALLBACK) {
    lua_pushnil(L);
    lua_rawseti(L, EVQ_CORO_CALLBACK, ev_id);
  }
  /* obj_udata */
  lua_pushnil(L);
  lua_rawseti(L, EVQ_CORO_UDATA, ev_id);

  levq_free_event(evq, ev);
}


/*
 * Arguments: evq_udata,
 *	obj_udata | signal (number),
 *	event (string: "r", "w") | event_flags (number),
 *	callback (function | coroutine),
 *	[timeout (milliseconds), one_shot (boolean)]
 * Returns: [ev_ludata]
 */
static int
levq_add (lua_State *L)
{
  struct event_queue *evq = checkudata(L, 1, EVQ_TYPENAME);
  unsigned int ev_flags = lua_tointeger(L, 3)
   | (lua_toboolean(L, 6) ? EVENT_ONESHOT : 0);
  const msec_t timeout = lua_isnoneornil(L, 5)
   ? TIMEOUT_INFINITE : (msec_t) lua_tointeger(L, 5);
  const char *dirwatch_path = NULL;
#ifdef _WIN32
  HKEY *regwatch_hkp = NULL;
#endif
  struct event *ev;
  int res;

  ev = levq_new_event(evq);

  if (!(ev_flags & (EVENT_TIMER | EVENT_DIRWATCH | EVENT_REGWATCH))) {
    fd_t *fdp = lua_touserdata(L, 2);
    ev->fd = fdp ? *fdp
     : (fd_t) (size_t) lua_tointeger(L, 2);  /* signo */
  } else if (ev_flags & EVENT_DIRWATCH) {
    dirwatch_path = luaL_checkstring(L, 2);
#ifdef _WIN32
  } else if (ev_flags & EVENT_REGWATCH) {
    regwatch_hkp = checkudata(L, 2, WREG_TYPENAME);
#endif
  }

  if (!(ev_flags & (EVENT_READ | EVENT_WRITE))) {
    const char *evstr = lua_tostring(L, 3);

    ev_flags |= (!evstr || evstr[0] == 'r')
     ? EVENT_READ : EVENT_WRITE;
  }

  switch (lua_type(L, 4)) {
  case LUA_TFUNCTION:
    ev_flags |= EVENT_CALLBACK;
    break;
  case LUA_TTHREAD:
    ev_flags |= EVENT_CALLBACK | EVENT_CALLBACK_CORO;
    break;
  }

  ev->flags = ev_flags;

  /* reserve place for timeout_queue */
  if (!evq->ev_free)
    evq->ev_free = levq_new_event(evq);

  levq_control_wait(evq, 1);
  if (ev_flags & EVENT_TIMER) {
    res = evq_add_timer(evq, ev, timeout);
  } else {
    if (ev_flags & EVENT_DIRWATCH) {
      res = evq_add_dirwatch(evq, ev, dirwatch_path);
#ifdef _WIN32
    } else if (ev_flags & EVENT_REGWATCH) {
      res = evq_add_regwatch(evq, ev, *regwatch_hkp);
#endif
    } else {
      res = evq_add(evq, ev);
    }

    if (!res && timeout != TIMEOUT_INFINITE) {
      evq_set_timeout(ev, timeout);
    }
  }
  levq_control_wait(evq, 0);

  if (!res) {
    lua_State *NL = evq->L;
    const int ev_id = ev->ev_id;

    /* cb_fun */
    if (ev_flags & EVENT_CALLBACK) {
      lua_settop(L, 4);
      lua_xmove(L, NL, 1);
      lua_rawseti(NL, EVQ_CORO_CALLBACK, ev_id);
    }
    /* obj_udata */
    lua_settop(L, 2);
    lua_xmove(L, NL, 1);
    lua_rawseti(NL, EVQ_CORO_UDATA, ev_id);

    lua_pushlightuserdata(L, ev);
    return 1;
  }
  levq_del_event(evq, ev);
  return sys_seterror(L, 0);
}

/*
 * Arguments: evq_udata, callback (function), timeout (milliseconds),
 *	[one_shot (boolean), object (any)]
 * Returns: [ev_ludata]
 */
static int
levq_add_timer (lua_State *L)
{
  lua_settop(L, 5);
  lua_insert(L, 2);  /* obj_udata */
  lua_pushinteger(L, EVENT_READ | EVENT_TIMER);  /* event_flags */
  lua_insert(L, 3);
  return levq_add(L);
}

/*
 * Arguments: evq_udata, pid_udata, callback (function),
 *	[timeout (milliseconds)]
 * Returns: [ev_ludata]
 */
static int
levq_add_pid (lua_State *L)
{
  lua_settop(L, 4);
  lua_pushinteger(L, EVENT_READ | EVENT_PID | EVENT_ONESHOT
#ifndef _WIN32
   | EVENT_SIGNAL
#endif
  );  /* event_flags */
  lua_insert(L, 3);
  return levq_add(L);
}

/*
 * Arguments: evq_udata, obj_udata, callback (function),
 *	[timeout (milliseconds), one_shot (boolean)]
 * Returns: [ev_ludata]
 */
static int
levq_add_winmsg (lua_State *L)
{
  lua_settop(L, 5);
  lua_pushinteger(L, EVENT_READ | EVENT_WINMSG);  /* event_flags */
  lua_insert(L, 3);
  return levq_add(L);
}

/*
 * Arguments: evq_udata, path (string), callback (function),
 *	[timeout (milliseconds), one_shot (boolean), modify (boolean)]
 * Returns: [ev_ludata]
 */
static int
levq_add_dirwatch (lua_State *L)
{
  const unsigned int filter = lua_toboolean(L, 6) ? EVENT_WATCH_MODIFY : 0;

  lua_settop(L, 5);
  lua_pushinteger(L, EVENT_READ | EVENT_DIRWATCH
   | filter);  /* event_flags */
  lua_insert(L, 3);
  return levq_add(L);
}

#ifdef _WIN32
/*
 * Arguments: evq_udata, reg_udata, callback (function),
 *	[timeout (milliseconds), one_shot (boolean),
 *	modify (boolean), recursive (boolean)]
 * Returns: [ev_ludata]
 */
static int
levq_add_regwatch (lua_State *L)
{
  const unsigned int filter = (lua_toboolean(L, 6) ? EVENT_WATCH_MODIFY : 0)
   | (lua_toboolean(L, 7) ? EVENT_WATCH_RECURSIVE : 0);

  lua_settop(L, 5);
  lua_pushinteger(L, EVENT_READ | EVENT_REGWATCH
   | filter);  /* event_flags */
  lua_insert(L, 3);
  return levq_add(L);
}
#endif

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
  lua_pushinteger(L, EVENT_READ | EVENT_SIGNAL);  /* event_flags */
  lua_insert(L, 3);
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
 *	event (string: "r", "w", "accept", "connect"),
 *	callback (function), [timeout (milliseconds), one_shot (boolean)]
 * Returns: [ev_ludata]
 */
static int
levq_add_socket (lua_State *L)
{
  const char *evstr = lua_tostring(L, 3);
  unsigned int ev_flags = EVENT_SOCKET;

  if (evstr) {
    switch (*evstr) {
    case 'a':  /* accept */
      ev_flags |= EVENT_SOCKET_ACC_CONN | EVENT_READ;
      break;
    case 'c':  /* connect */
      ev_flags |= EVENT_SOCKET_ACC_CONN | EVENT_WRITE | EVENT_ONESHOT;
      break;
    default:
      ev_flags |= (evstr[0] == 'r') ? EVENT_READ : EVENT_WRITE;
    }
  } else {
    ev_flags |= EVENT_READ;
  }
  lua_settop(L, 6);
  lua_pushinteger(L, ev_flags);  /* event_flags */
  lua_replace(L, 3);
  return levq_add(L);
}

/*
 * Arguments: evq_udata, ev_ludata, events (string: "r", "w")
 * Returns: [evq_udata]
 */
static int
levq_mod_socket (lua_State *L)
{
  struct event_queue *evq = checkudata(L, 1, EVQ_TYPENAME);
  struct event *ev = levq_toevent(L, 2);
  const char *evstr = luaL_checkstring(L, 3);
  const unsigned int rw_flags = (evstr[0] == 'r')
   ? EVENT_READ : EVENT_WRITE;
  int res;

  levq_control_wait(evq, 1);
  lua_assert(ev && !event_deleted(ev) && (ev->flags & EVENT_SOCKET)
   && rw_flags != (ev->flags & (EVENT_READ | EVENT_WRITE)));

  res = evq_modify(ev, rw_flags);
  levq_control_wait(evq, 0);

  if (!res) {
    ev->flags &= ~(EVENT_READ | EVENT_WRITE);
    ev->flags |= rw_flags;
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

  levq_control_wait(evq, 1);
  if (!event_deleted(ev)) {
    res = evq_del(ev, reuse_fd);
  }
  levq_control_wait(evq, 0);

  if (!(ev->flags & (EVENT_ACTIVE | EVENT_DELETE)))
    levq_del_event(evq, ev);

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
  struct event_queue *evq = checkudata(L, 1, EVQ_TYPENAME);
  struct event *ev = levq_toevent(L, 2);
  lua_State *NL = evq->L;
  const int top = lua_gettop(L);

  lua_assert(ev && !event_deleted(ev));

  if (top < 3) {
    lua_rawgeti(NL, EVQ_CORO_CALLBACK, ev->ev_id);
    lua_xmove(NL, L, 1);
  } else {
    ev->flags &= ~(EVENT_CALLBACK | EVENT_CALLBACK_CORO);
    if (!lua_isnoneornil(L, 3)) {
      ev->flags |= EVENT_CALLBACK
       | (lua_isthread(L, 3) ? EVENT_CALLBACK_CORO : 0);
    }

    lua_settop(L, 3);
    lua_xmove(L, NL, 1);
    lua_rawseti(NL, EVQ_CORO_CALLBACK, ev->ev_id);
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
  int res;

  levq_control_wait(evq, 1);
  lua_assert(ev && !event_deleted(ev) && !(ev->flags & EVENT_WINMSG));

  /* reserve place for timeout_queue */
  if (!evq->ev_free)
    evq->ev_free = levq_new_event(evq);

  res = evq_set_timeout(ev, timeout);
  levq_control_wait(evq, 0);

  if (!res) {
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


static void
levq_sync_process (lua_State *L, struct event_queue *evq,
                   struct evq_sync_op *op)
{
  const int top = lua_gettop(L);

  do {
    lua_State *NL = op->L;

    if (op->is_sched_add) {
      struct event *ev = (struct event *) op;
      const int fn_idx = op->fn_idx;

      op = op->next;
      levq_free_event(evq, ev);

      if (NL) {
        const int narg = lua_gettop(NL) - fn_idx;

        lua_xmove(NL, L, narg + 1);
        lua_call(L, narg, LUA_MULTRET);

        ev = lua_touserdata(L, top + 1);
        if (ev) {
          ev->flags |= EVENT_CALLBACK_SCHED;
          lua_pop(L, 1);  /* pop ev_ludata */
        } else {
          /* error */
          lua_xmove(L, NL, lua_gettop(L) - top);
        }
        sys_sched_event_added(NL, ev);
      }
    } else {
      const int narg = lua_gettop(NL) - op->fn_idx;

      op->status = lua_pcall(NL, narg, LUA_MULTRET, 0);
      sys_thread_resume(op->td);
      op = op->next;
    }
  } while (op);
}

/*
 * Arguments: ..., function, [arguments (any) ...]
 * Returns: [results (any) ...]
 */
static int
levq_sync_call (lua_State *L, struct event_queue *evq,
                struct evq_sync_op *op,
                const int is_sched_add, const int fn_idx)
{
  op->L = L;
  op->fn_idx = fn_idx;
  op->status = 0;
  op->is_sched_add = is_sched_add;
  op->next = evq->sync_op;

  evq->sync_op = op;
  if (evq->flags & EVQ_FLAG_WAITING)
    evq_signal(evq, EVQ_SIGEVQ);

  if (is_sched_add) {
    lua_pushlightuserdata(L, op);  /* sync_op_ludata */
    return 0;
  } else {
    struct sys_thread *td = sys_thread_get();
    const int old_top = fn_idx - 1;

    if (!td) luaL_argerror(L, 0, "Threading not initialized");

    op->td = td;
    sys_thread_suspend(td, TIMEOUT_INFINITE);  /* wait result */

    if (op->status) lua_error(L);
    return lua_gettop(L) - old_top;
  }
}

/*
 * Arguments: evq_udata, function, [arguments (any) ...]
 * Returns: [results (any) ...]
 */
static int
levq_sync (lua_State *L)
{
  struct event_queue *evq = checkudata(L, 1, EVQ_TYPENAME);
  struct evq_sync_op op;

  luaL_checktype(L, 2, LUA_TFUNCTION);

  return levq_sync_call(L, evq, &op, 0, 2);
}

#define ARG_EXTRAS	2
/*
 * Arguments: ..., evq_udata
 */
int
sys_evq_loop (lua_State *L, struct event_queue *evq,
              const msec_t timeout, const int linger, const int once,
              const int evq_idx)
{
  struct sys_thread *td = sys_thread_get();
  int res = 0;

  /* push callback and object tables */
  {
    lua_State *NL = evq->L;

    lua_pushvalue(NL, EVQ_CORO_CALLBACK);
    lua_pushvalue(NL, EVQ_CORO_UDATA);
    lua_xmove(NL, L, ARG_EXTRAS);
  }

  while (!(evq->flags & EVQ_FLAG_STOP)) {
    struct event *ev;

    /* process synchronous operations */
    if (evq->sync_op) {
      struct evq_sync_op *op = evq->sync_op;

      evq->sync_op = NULL;
      levq_sync_process(L, evq, op);
    }

    if (!evq->ev_ready) {
      if (!linger && evq_is_empty(evq))
        break;

      if (td) {
        for (; ; ) {
          sys_thread_check(td, L);

          if (!(evq->nactives || (evq->flags & EVQ_FLAG_WAITING)))
            break;

          evq->nidles++;
          res = thread_event_wait(&evq->wait_tev, td, TIMEOUT_INFINITE);
          evq->nidles--;

          if (res || evq->ev_ready || (evq->flags & EVQ_FLAG_STOP))
            goto no_wait;
        }
      }

      evq->flags |= EVQ_FLAG_WAITING;
      res = evq_wait(evq, td, timeout);
      evq->flags &= ~EVQ_FLAG_WAITING;

 no_wait:
      if (res) break;
    }

    ev = evq->ev_ready;
    if (!ev) continue;

    evq->nactives++;
    /* notify another thread to process ready events */
    if (evq->nidles && ev->next_ready)
      thread_event_signal(&evq->wait_tev);

    do {
      const unsigned int ev_flags = ev->flags;

      /* clear EVENT_ACTIVE and EVENT_*_RES flags */
      ev->flags &= ~EVENT_MASK_RES;
      evq->ev_ready = ev->next_ready;

      if (ev_flags & EVENT_DELETE) {
        /* postponed deletion of active event */
        levq_del_event(evq, ev);
      } else {
        if (ev_flags & EVENT_CALLBACK) {
          const int ev_id = ev->ev_id;

          /* callback function */
          lua_rawgeti(L, evq_idx+1, ev_id);
          /* arguments */
          if (!(ev_flags & EVENT_CALLBACK_SCHED)) {
            lua_pushvalue(L, 1);  /* evq_udata */
            lua_pushlightuserdata(L, ev);  /* ev_ludata */
            lua_rawgeti(L, evq_idx+2, ev_id);  /* obj_udata */
          }
          lua_pushstring(L,
           (ev_flags & EVENT_READ_RES) ? "r"
           : (ev_flags & EVENT_WRITE_RES) ? "w"
           : (ev_flags & EVENT_TIMEOUT_RES) ? "t" : "e");
          if (ev_flags & EVENT_EOF_RES)
            lua_pushboolean(L, 1);
          else if (ev_flags & EVENT_PID)
            lua_pushinteger(L,
             (int) ev_flags >> EVENT_STATUS_SHIFT);
          else
            lua_pushnil(L);
        }

        if ((ev_flags & EVENT_ONESHOT) && !event_deleted(ev))
          evq_del(ev, 1);

        if (event_deleted(ev))
          levq_del_event(evq, ev);  /* deletion of oneshot event */

        if (ev_flags & EVENT_CALLBACK_SCHED) {
          /* callback function: coroutine */
          lua_State *co = lua_tothread(L, evq_idx + ARG_EXTRAS + 1);

          lua_xmove(L, co, 2);
          lua_pop(L, 1);  /* pop coroutine */

          sys_sched_event_ready(co, ev);
        } else if (ev_flags & EVENT_CALLBACK_CORO) {
          lua_State *co = lua_tothread(L, evq_idx + ARG_EXTRAS + 1);

          lua_xmove(L, co, 5);
          lua_pop(L, 1);  /* pop coroutine */

          switch (lua_resume(co, L, 5)) {
          case 0:
            lua_settop(co, 0);
            if (!event_deleted(ev)) {
              evq_del(ev, 0);
              levq_del_event(evq, ev);
            }
            break;
          case LUA_YIELD:
            res = 0;
            lua_settop(co, 0);
            break;
          default:
            lua_xmove(co, L, 1);  /* error message */
            res = SYS_ERR_THROW;
          }
        } else if (ev_flags & EVENT_CALLBACK) {
          if (lua_pcall(L, 5, 0, 0)) {
            res = SYS_ERR_THROW;
          }
        }

#ifdef EVQ_POST_INIT
        if (!event_deleted(ev))
          (void) evq_post_init(ev);
#endif
        if (res) break;  /* error */
      }
      ev = evq->ev_ready;
    } while (ev);

    evq->nactives--;

    if (res || once) break;
  }

  if (evq->nidles && !(evq->flags & EVQ_FLAG_WAITING))
    thread_event_signal(&evq->wait_tev);

  return res;
}
#undef ARG_EXTRAS

/*
 * Arguments: evq_udata, [timeout (milliseconds),
 *	linger (boolean), once (boolean)]
 * Returns: [evq_udata | timeout (false)]
 */
static int
levq_loop (lua_State *L)
{
  struct event_queue *evq = checkudata(L, 1, EVQ_TYPENAME);
  const msec_t timeout = (lua_type(L, 2) != LUA_TNUMBER)
   ? TIMEOUT_INFINITE : (msec_t) lua_tointeger(L, 2);
  const int linger = lua_toboolean(L, 3);  /* keep running on empty queue */
  const int once = lua_toboolean(L, 4);  /* process one cycle of active events */

  lua_settop(L, 1);

  switch (sys_evq_loop(L, evq, timeout, linger, once, 1)) {
  case SYS_ERR_TIMEOUT:
    lua_pushboolean(L, 0);
    return 1;  /* timed out */
  case SYS_ERR_SYSTEM:
    return sys_seterror(L, 0);
  case SYS_ERR_THROW:
    lua_error(L);
  }
  lua_settop(L, 1);
  return 1;
}

/*
 * Arguments: evq_udata, [stop/work (boolean)]
 */
static int
levq_stop (lua_State *L)
{
  struct event_queue *evq = checkudata(L, 1, EVQ_TYPENAME);
  const int stop = lua_isnoneornil(L, 2) || lua_toboolean(L, 2);

  if (stop) {
    evq->flags |= EVQ_FLAG_STOP;
  } else {
    evq->flags = ~EVQ_FLAG_STOP;
  }
  if (evq->flags & EVQ_FLAG_WAITING)
    evq_signal(evq, EVQ_SIGEVQ);
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
    evq->now = sys_milliseconds();
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

  evq->ev_ready = evq_process_active(ev, evq->ev_ready, evq->now);

  if (evq->flags & EVQ_FLAG_WAITING)
    evq_signal(evq, EVQ_SIGEVQ);

  lua_settop(L, 1);
  return 1;
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


/*
 * Arguments: ..., evq_udata, arguments ...
 * Returns: [nil, error_message]
 */
int
sys_evq_sched_add (lua_State *L, const int evq_idx, const int type)
{
  struct event_queue *evq = checkudata(L, evq_idx, EVQ_TYPENAME);
  lua_CFunction func;

  switch (type) {
  case EVQ_SCHED_TIMER:
    func = levq_add_timer;
    break;
  case EVQ_SCHED_PID:
    func = levq_add_pid;
    break;
  case EVQ_SCHED_DIRWATCH:
    func = levq_add_dirwatch;
    break;
#ifdef _WIN32
  case EVQ_SCHED_REGWATCH:
    func = levq_add_regwatch;
    break;
#endif
  case EVQ_SCHED_SIGNAL:
    func = levq_add_signal;
    break;
  case EVQ_SCHED_SOCKET:
    func = levq_add_socket;
    break;
  default:
    func = levq_add;
  }

  lua_pushcfunction(L, func);  /* function */
  lua_insert(L, evq_idx);

  if (!(evq->flags & EVQ_FLAG_WAITING)) {
    struct event *ev;

    /* result: ev_ludata | nil, error_message */
    lua_call(L, lua_gettop(L) - evq_idx, LUA_MULTRET);
    ev = lua_touserdata(L, evq_idx);
    if (ev) {
      ev->flags |= EVENT_CALLBACK_SCHED;
      sys_sched_event_added(L, ev);
      lua_pop(L, 1);  /* pop ev_ludata */
      return 0;
    }
    return -1;
  }

  return levq_sync_call(L, evq,
   (struct evq_sync_op *) levq_new_event(evq), 1, evq_idx);
}

/*
 * Arguments: ..., evq_udata
 * Returns: [nil, error_message]
 */
void
sys_evq_sched_del (lua_State *L, void *ev_op, const int ev_added)
{
  struct event_queue *evq = checkudata(L, -1, EVQ_TYPENAME);

  if (ev_added) {
    struct event *ev = (struct event *) ev_op;

    if (event_deleted(ev) || (ev->flags & EVENT_ACTIVE))
      return;

    ev->flags |= EVENT_ACTIVE | EVENT_ONESHOT;
    ev->flags &= ~(EVENT_CALLBACK | EVENT_CALLBACK_CORO | EVENT_CALLBACK_SCHED);

    ev->next_ready = evq->ev_ready;
    evq->ev_ready = ev;
  } else {
    struct evq_sync_op *sync_op = (struct evq_sync_op *) ev_op;
    sync_op->L = NULL;
  }

  if (evq->flags & EVQ_FLAG_WAITING)
    evq_signal(evq, EVQ_SIGEVQ);
}


#define EVQ_METHODS \
  {"event_queue",	levq_new}

static luaL_Reg evq_meth[] = {
  {"add",		levq_add},
  {"add_timer",		levq_add_timer},
  {"add_pid",		levq_add_pid},
  {"add_winmsg",	levq_add_winmsg},
  {"add_dirwatch",	levq_add_dirwatch},
#ifdef _WIN32
  {"add_regwatch",	levq_add_regwatch},
#endif
  {"add_signal",	levq_add_signal},
  {"ignore_signal",	levq_ignore_signal},
  {"signal",		levq_signal},
  {"add_socket",	levq_add_socket},
  {"mod_socket",	levq_mod_socket},
  {"del",		levq_del},
  {"timeout",		levq_timeout},
  {"timeout_manual",	levq_timeout_manual},
  {"callback",		levq_callback},
  {"sync",		levq_sync},
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
