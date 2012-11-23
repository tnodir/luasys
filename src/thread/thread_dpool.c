/* Lua System: Threading: Data Pool */

#define DPOOL_TYPENAME	"sys.thread.data_pool"

struct data_pool {
  unsigned int volatile n;  /* count of data in storage */

  int volatile nwaits;  /* number of blocked readers */
  int volatile nput; /* number of items to put as new data */
  lua_State * volatile L;  /* data writer */

  unsigned int idx, top;  /* storage indexes */
  unsigned int max;  /* maximum watermark of data */

#define DPOOL_PUTONFULL		1
#define DPOOL_GETONEMPTY	2
#define DPOOL_OPEN		4
  unsigned int flags;

  thread_event_t tev;  /* synchronization */
};


/*
 * Returns: [dpool_udata]
 */
static int
dpool_new (lua_State *L)
{
  struct data_pool *dp = lua_newuserdata(L, sizeof(struct data_pool));
  memset(dp, 0, sizeof(struct data_pool));
  dp->max = (unsigned int) -1;

  if (!thread_event_new(&dp->tev)) {
    dp->flags |= DPOOL_OPEN;
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
dpool_close (lua_State *L)
{
  struct data_pool *dp = checkudata(L, 1, DPOOL_TYPENAME);

  if (dp->flags & DPOOL_OPEN) {
    dp->flags ^= DPOOL_OPEN;
    thread_event_del(&dp->tev);
  }
  return 0;
}

/*
 * Arguments: dpool_udata, data_items (any) ...
 */
static int
dpool_put (lua_State *L)
{
  struct sys_thread *td = sys_thread_get();
  struct data_pool *dp = checkudata(L, 1, DPOOL_TYPENAME);
  int nput = lua_gettop(L) - 1;

  if (!td) luaL_argerror(L, 0, "Threading not initialized");
  if (!nput) luaL_argerror(L, 2, "data expected");

  lua_getfenv(L, 1);  /* storage */
  lua_insert(L, 1);

  if (dp->n >= dp->max) {
    if (dp->flags & DPOOL_PUTONFULL) {
      lua_rawgetp(L, 1, (void *) DPOOL_PUTONFULL);
      lua_insert(L, 2);
      lua_call(L, 1 + nput, LUA_MULTRET);
      nput = lua_gettop(L) - 1;
      if (!nput) return 0;
    } else {
      do {
        const int res = thread_event_wait(&dp->tev, td,
         TIMEOUT_INFINITE);

        sys_thread_check(td, L);
        if (res) return sys_seterror(L, 0);
      } while (dp->n >= dp->max);
    }
  }

  /* Try directly move data between threads */
  if (dp->nwaits && !dp->L) {
    dp->L = L;
    dp->nput = nput;
    thread_event_signal(&dp->tev);
    sys_thread_switch(td);
    dp->L = NULL;
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

    if (!dp->n++) {
      thread_event_signal(&dp->tev);
    }
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
  struct sys_thread *td = sys_thread_get();
  struct data_pool *dp = checkudata(L, 1, DPOOL_TYPENAME);
  const msec_t timeout = lua_isnoneornil(L, 2)
   ? TIMEOUT_INFINITE : (msec_t) lua_tointeger(L, 2);
  int nput;

  if (!td) luaL_argerror(L, 0, "Threading not initialized");

  lua_settop(L, 1);
  lua_getfenv(L, 1);  /* storage */
  lua_insert(L, 1);

  if ((dp->flags & DPOOL_GETONEMPTY) && !dp->n) {
    lua_rawgetp(L, 1, (void *) DPOOL_GETONEMPTY);
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
        thread_event_signal(&dp->tev);
      }
      return nput;
    }

    /* wait signal */
    {
      int res;

      dp->nwaits++;
      res = thread_event_wait(&dp->tev, td, timeout);
      dp->nwaits--;

      sys_thread_check(td, L);
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
      luaL_checkstack(L, nput, NULL);
      lua_xmove(dp->L, L, nput);
      dp->nput = 0;
      return nput;
    }
  }
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

  lua_pushvalue(L, 2);
  lua_rawsetp(L, -2, (void *) DPOOL_PUTONFULL);

  lua_pushvalue(L, 3);
  lua_rawsetp(L, -2, (void *) DPOOL_GETONEMPTY);

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

  lua_pushfstring(L, DPOOL_TYPENAME " (%p)", dp);
  return 1;
}


#define DPOOL_METHODS \
  {"data_pool",		dpool_new}

static luaL_Reg dpool_meth[] = {
  {"put",		dpool_put},
  {"get",		dpool_get},
  {"max",		dpool_max},
  {"callbacks",		dpool_callbacks},
  {"__len",		dpool_count},
  {"__tostring",	dpool_tostring},
  {"__gc",		dpool_close},
  {NULL, NULL}
};
