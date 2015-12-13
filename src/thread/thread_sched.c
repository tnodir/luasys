/* Lua System: Threading: Scheduler */

#define SCHED_TYPENAME	"sys.thread.scheduler"

#define SCHED_BUF_MIN		32
#define SCHED_WORKER_TIMEOUT	3000

/* Scheduler coroutine reserved indexes */
#define SCHED_CORO_ENV		1  /* environ. */
#define SCHED_CORO_CBCOCTL	2  /* callback: coroutines controller */
#define SCHED_CORO_UDATA	3  /* sched_udata */
#define SCHED_CORO_EVQ		4  /* evq_udata */

struct sched_task {
  lua_State *co;  /* coroutine */

  int prev_id, next_id;  /* circular list of tasks */
  void *ev_op;  /* waiting in event queue */

  unsigned int started:		1;  /* execution started */
  unsigned int suspended:	1;  /* execution was suspended */
  unsigned int ev_added:	1;  /* event added to event queue */
  unsigned int terminate:	1;  /* termination requested */
};

struct scheduler {
  lua_State *L;  /* storage */

  struct event_queue *evq;  /* event queue */

  int active_task_id;  /* active tasks */
  int free_task_id;  /* free tasks */

  unsigned int ntasks;  /* number of tasks */
  unsigned int volatile tick;  /* yields count */

  struct sched_context *ctx;  /* running context */

  struct sched_task *buffer;  /* tasks buffer */
  int buf_idx, buf_max;  /* tasks buffer current and maximum indexes */

  unsigned int stop:		1;  /* stop looping */
  unsigned int cb_coctl:	1;  /* coroutines controller exists */
  unsigned int evq_waiting:	1;  /* waiting in event queue */

  msec_t worker_timeout;  /* worker thread timeout */

  unsigned short min_workers;  /* minimum number of worker threads */
  unsigned short max_workers;  /* maximum number of worker threads */

  unsigned short nworkers;  /* number of worker threads */
  unsigned short nwaiters;  /* number of waiting threads */

  thread_critsect_t cs;  /* guard access to context */
  thread_cond_t cond;  /* notification */
};

struct sched_context {
  struct scheduler *sched;
  lua_State *co;  /* running task coroutine */
  int task_id;  /* running task */
};


#define sched_is_empty(sched)	(!sched->ntasks)

#define sched_check_task_id(sched,task_id) \
    (task_id >= 0 && task_id < (sched)->buf_idx)

#define sched_id_to_task(sched,task_id) \
    (&(sched)->buffer[task_id])

#define sched_task_to_id(sched,task) \
    (task - (sched)->buffer)

#define sched_tasklist_add(sched,list_idp,task) \
  do { \
    const int task_id_ = sched_task_to_id(sched, task); \
    const int head_id_ = *(list_idp); \
    if (head_id_ != -1) { \
      struct sched_task *head_task_ = \
       sched_id_to_task(sched, head_id_); \
      struct sched_task *head_prev_ = \
       sched_id_to_task(sched, head_task_->prev_id); \
      head_prev_->next_id = task_id_; \
      task->prev_id = head_task_->prev_id; \
      head_task_->prev_id = task_id_; \
      task->next_id = head_id_; \
    } else { \
      task->prev_id = task->next_id = task_id_; \
      *(list_idp) = task_id_; \
    } \
  } while (0)

#define sched_tasklist_del(sched,list_idp,task) \
  do { \
    const int task_id_ = sched_task_to_id(sched, task); \
    if (task_id_ != task->next_id) { \
      struct sched_task *task_prev_ = \
       sched_id_to_task(sched, task->prev_id); \
      struct sched_task *task_next_ = \
       sched_id_to_task(sched, task->next_id); \
      task_next_->prev_id = task->prev_id; \
      task_prev_->next_id = task->next_id; \
      if (*(list_idp) == task_id_) { \
        *(list_idp) = task->next_id; \
      } \
    } else { \
      *(list_idp) = -1; \
    } \
  } while (0)


static struct sched_task *
sched_task_alloc (struct scheduler *sched)
{
  struct sched_task *task;

  if (sched->free_task_id != -1) {
    const int task_id = sched->free_task_id;
    task = sched_id_to_task(sched, task_id);
    sched_tasklist_del(sched, &sched->free_task_id, task);
  } else {
    if (sched->buf_idx == sched->buf_max) {
      const int newlen = sched->buf_max
       ? sched->buf_max * 2 : SCHED_BUF_MIN;
      void *p = realloc(sched->buffer,
       newlen * sizeof(struct sched_task));

      if (!p) return NULL;

      sched->buffer = p;
      sched->buf_max = newlen;
    }
    task = sched_id_to_task(sched, sched->buf_idx++);
  }
  return task;
}

static void
sched_task_del (lua_State *L, struct scheduler *sched,
                struct sched_task *task, const int error)
{
  lua_State *NL = sched->L;
  lua_State *co = task->co;

  lua_assert(!task->ev_op);

  sched_tasklist_add(sched, &sched->free_task_id, task);
  sched->ntasks--;

  if (sched->cb_coctl) {
    lua_pushvalue(sched->L, SCHED_CORO_CBCOCTL);
    lua_xmove(sched->L, L, 1);  /* function */
    lua_pushthread(co);
    lua_xmove(co, L, 1);  /* coroutine */
    if (error) lua_xmove(co, L, 1);  /* error message */
    lua_settop(co, 0);
  }

  /* remove task coroutine from scheduler */
  lua_pushnil(NL);
  lua_rawseti(NL, SCHED_CORO_ENV,
   sched_task_to_id(sched, task));  /* task_id -> coro */

  if (sched->cb_coctl) {
    lua_call(L, (error ? 2 : 1), 0);
  }
}

#define ARG_LAST	1
/*
 * Arguments: [coroutines_controller (function),
 *	min_workers (number), max_workers (number),
 *	worker_timeout (milliseconds)]
 * Returns: [sched_udata]
 */
static int
sched_new (lua_State *L)
{
  const int min_workers = lua_tointeger(L, 2);
  const int max_workers = luaL_optinteger(L, 3, min_workers);
  const msec_t worker_timeout = luaL_optinteger(L, 4, SCHED_WORKER_TIMEOUT);
  struct scheduler *sched;
  lua_State *NL;

  lua_settop(L, ARG_LAST);

  sched = lua_newuserdata(L, sizeof(struct scheduler));
  memset(sched, 0, sizeof(struct scheduler));
  luaL_getmetatable(L, SCHED_TYPENAME);
  lua_setmetatable(L, -2);

  lua_newtable(L);  /* environ. (SCHED_CORO_ENV) */
  lua_pushvalue(L, -1);
  lua_setfenv(L, -3);

  NL = lua_newthread(L);
  if (!NL) return 0;

  sched->active_task_id
   = sched->free_task_id = -1;

  sched->min_workers = min_workers;
  sched->max_workers = max_workers;
  sched->worker_timeout = worker_timeout;

  sched->L = NL;
  lua_rawsetp(L, -2, NL);  /* save coroutine to avoid GC */

  sched->cb_coctl = lua_isfunction(L, 1);

  lua_pushvalue(L, 1);  /* coroutines controller (SCHED_CORO_CBCOCTL) */
  lua_pushvalue(L, ARG_LAST+1);  /* sched_udata (SCHED_CORO_UDATA) */
  lua_pushnil(L);  /* SCHED_CORO_EVQ */
  lua_xmove(L, NL, 4);

  if (!thread_critsect_new(&sched->cs)
   && !thread_cond_new(&sched->cond)) {
    return 1;
  }
  return sys_seterror(L, 0);
}
#undef ARG_LAST

/*
 * Arguments: sched_udata
 */
static int
sched_close (lua_State *L)
{
  struct scheduler *sched = checkudata(L, 1, SCHED_TYPENAME);

  (void) thread_critsect_del(&sched->cs);
  (void) thread_cond_del(&sched->cond);

  if (sched->buffer) {
    free(sched->buffer);
    sched->buffer = NULL;
  }
  return 0;
}

/*
 * Arguments: sched_udata, [evq_udata]
 * Returns: sched_udata | evq_udata
 */
static int
sched_event_queue (lua_State *L)
{
  struct scheduler *sched = checkudata(L, 1, SCHED_TYPENAME);

  if (lua_gettop(L) > 1) {
    struct event_queue *evq = checkudata(L, 2, EVQ_TYPENAME);

    if (!sched_is_empty(sched))
      luaL_argerror(L, 2, "Scheduler must be empty");

    sched->evq = evq;

    lua_settop(L, 2);
    lua_xmove(L, sched->L, 1);
    lua_replace(sched->L, SCHED_CORO_EVQ);
  } else if (sched->evq) {
    lua_pushvalue(sched->L, SCHED_CORO_EVQ);
    lua_xmove(sched->L, L, 1);
  } else {
    lua_pushnil(L);
  }
  return 1;
}


static void
sched_vm_switch (struct sched_context *sched_ctx, const int enter_vm)
{
  struct scheduler *sched = sched_ctx->sched;
  thread_critsect_t *csp = &sched->cs;

  thread_critsect_enter(csp);
  sched->ctx = enter_vm ? sched_ctx : NULL;
  thread_critsect_leave(csp);
}

/*
 * Arguments: thread_udata
 * Returns: [sched_udata]
 */
static int
sched_self (lua_State *L)
{
  struct sys_thread *td = checkudata(L, 1, THREAD_TYPENAME);
  struct sched_context *sched_ctx = td->sched_ctx;

  if (sched_ctx) {
    struct scheduler *sched = sched_ctx->sched;

    lua_pushvalue(sched->L, SCHED_CORO_UDATA);
    lua_xmove(sched->L, L, 1);
    return 1;
  }
  return 0;
}

/*
 * Arguments: sched_udata, [timeout (milliseconds),
 *	not_linger (boolean), once (boolean)]
 * Returns: [sched_udata | timedout (false)]
 */
static int
sched_loop (lua_State *L)
{
  struct sys_thread *td = sys_thread_get();
  struct scheduler *sched = checkudata(L, 1, SCHED_TYPENAME);
  const msec_t timeout = lua_isnoneornil(L, 2)
   ? TIMEOUT_INFINITE : (msec_t) lua_tointeger(L, 2);
  const int not_linger = lua_toboolean(L, 3);  /* don't keep running on empty queue */
  const int once = lua_toboolean(L, 4);  /* process only one event */
  thread_critsect_t *csp = &sched->cs;
  struct sched_context sched_ctx;
  int err = 0;

  if (!td) luaL_argerror(L, 0, "Threading not initialized");

  sched->nworkers++;

  sched_ctx.sched = sched;
  sched_ctx.co = NULL;

  td->sched_ctx = &sched_ctx;

  thread_critsect_enter(csp);
  sched->ctx = &sched_ctx;
  thread_critsect_leave(csp);

  lua_settop(L, 1);

  for (; ; ) {
    struct sched_task *task;
    lua_State *co;
    const int task_id = sched->active_task_id;
    int res, narg;

    if (sched->stop) {
      (void) thread_cond_signal(&sched->cond);
      break;
    }

    if (task_id == -1) {
      if (not_linger) break;

      if (sched->nwaiters == (sched->nworkers - 1)
       && sched->evq && !sched->evq_waiting) {
        lua_pushvalue(sched->L, SCHED_CORO_EVQ);
        lua_xmove(sched->L, L, 1);  /* evq_udata */

        sched->evq_waiting = 1;
        err = sys_evq_loop(L, sched->evq, TIMEOUT_INFINITE,
         0 /* linger */, 1 /* once */, 2 /* evq_idx */);
        sched->evq_waiting = 0;

        if (!err) lua_settop(L, 1);
        else break;
      } else {
        sched->nwaiters++;
        res = thread_cond_wait_vm(&sched->cond, td, timeout);
        sched->nwaiters--;

        sys_thread_check(td, L);
        if (res) {
          err = (res == 1) ? SYS_ERR_TIMEOUT : SYS_ERR_SYSTEM;
          break;
        }
      }
      continue;
    }

    task = sched_id_to_task(sched, task_id);
    sched_tasklist_del(sched, &sched->active_task_id, task);

    if (task->terminate) {
      sched_task_del(L, sched, task, 0);
      continue;
    }

    co = task->co;
    narg = lua_gettop(co);

    if (!task->started) {
      task->started = -1;
      --narg;  /* - function */
    }

    thread_critsect_enter(csp);
    sched_ctx.task_id = task_id;
    sched_ctx.co = co;
    sched->tick++;
    thread_critsect_leave(csp);

    res = lua_resume(co, L, narg);  /* call coroutine */

    thread_critsect_enter(csp);
    sched_ctx.co = NULL;
    thread_critsect_leave(csp);

    /* buffer may be realloc'ed */
    task = sched_id_to_task(sched, task_id);

    switch (res) {
    case LUA_YIELD:
      if (!task->terminate) {
        if (!task->suspended)
          sched_tasklist_add(sched, &sched->active_task_id, task);
        break;
      }
      res = 0;
      /* FALLTHROUGH */
    case 0:
      /* FALLTHROUGH */
    default:
      if (res) {
        lua_pushvalue(co, -1);  /* error_message */
        lua_xmove(co, L, 1);
      }
      sched_task_del(L, sched, task, res);
      if (res) {
        err = SYS_ERR_THROW;
        goto end;
      }
    }

    if (once) break;
  }

 end:
  thread_critsect_enter(csp);
  sched->ctx = NULL;
  thread_critsect_leave(csp);

  td->sched_ctx = NULL;

  sched->nworkers--;

  if (!sched->stop && !sched->evq_waiting && sched->evq
   && sched->nwaiters == sched->nworkers)
    (void) thread_cond_signal(&sched->cond);

  switch (err) {
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
 * Arguments: sched_udata, [stop/work (boolean)]
 */
static int
sched_stop (lua_State *L)
{
  struct scheduler *sched = checkudata(L, 1, SCHED_TYPENAME);
  const int stop = lua_isnoneornil(L, 2) || lua_toboolean(L, 2);

  sched->stop = stop;
  (void) thread_cond_signal(&sched->cond);
  return 0;
}

/*
 * Arguments: sched_udata, thread_ludata
 */
static int
sched_thread_start (lua_State *L)
{
  struct scheduler *sched = checkudata(L, 1, SCHED_TYPENAME);
  struct sys_thread *td = lua_touserdata(L, 2);

  sys_thread_resume(td);

  lua_settop(L, 1);
  if (sched->nworkers == sched->min_workers) {
    lua_pushinteger(L, sched->worker_timeout);
  }
  sched_loop(L);
  return 0;
}

/*
 * Arguments: sched_udata, ...
 */
static void
sched_thread_run (lua_State *L)
{
  struct sys_thread *td = sys_thread_get();

  lua_pushcfunction(L, thread_run);
  lua_pushcfunction(L, sched_thread_start);
  lua_pushvalue(L, 1);  /* sched_udata */
  lua_pushlightuserdata(L, td);  /* thread_ludata */
  lua_call(L, 3, 0);

  sys_thread_suspend(td, TIMEOUT_INFINITE);
}

/*
 * Arguments: sched_udata, function | coroutine, [arguments ...]
 * Returns: [task_id]
 */
static int
sched_put (lua_State *L)
{
  struct scheduler *sched = checkudata(L, 1, SCHED_TYPENAME);
  lua_State *co = lua_tothread(L, 2);
  const int nmove = lua_gettop(L) - (co ? 2 : 1);
  lua_State *NL = sched->L;
  struct sched_task *task;
  int task_id;

  if (co) {
    lua_pushvalue(L, 2);  /* coroutine */
  } else if (sched->cb_coctl) {
    lua_pushvalue(sched->L, SCHED_CORO_CBCOCTL);
    lua_xmove(sched->L, L, 1);  /* function */
    lua_call(L, 0, 1);
    co = lua_tothread(L, -1);
    if (!co) lua_pop(L, 1);
  }
  if (!co) {
    co = lua_newthread(L);
    if (!co) return 0;
  }

  luaL_checkstack(co, nmove, NULL);

  task = sched_task_alloc(sched);
  if (!task) return 0;

  memset(task, 0, sizeof(struct sched_task));
  task->co = co;

  /* store task coroutine in scheduler */
  task_id = sched_task_to_id(sched, task);
  lua_xmove(L, NL, 1);  /* move task coroutine */
  lua_rawseti(NL, SCHED_CORO_ENV, task_id);  /* task_id -> coro */

  lua_xmove(L, co, nmove);  /* move function and arguments */

  /* add to active list */
  {
    const int is_empty = (sched->active_task_id == -1);

    sched_tasklist_add(sched, &sched->active_task_id, task);
    sched->ntasks++;

    /* notify or create new worker thread */
    if (is_empty) {
      if (sched->nwaiters)
        (void) thread_cond_signal(&sched->cond);
      else if (sched->nworkers < sched->max_workers)
        sched_thread_run(L);
    }
  }
  lua_pushinteger(L, task_id);
  return 1;
}

/*
 * Arguments: sched_udata
 * Returns: [task_id]
 */
static int
sched_running (lua_State *L)
{
  struct scheduler *sched = checkudata(L, 1, SCHED_TYPENAME);
  struct sched_context *sched_ctx = sched->ctx;

  if (sched_ctx && L == sched_ctx->co) {
    lua_pushinteger(L, sched_ctx->task_id);
    return 1;
  }
  return 0;
}

/*
 * Arguments: sched_udata, task_id
 * Returns: sched_udata
 */
static int
sched_terminate (lua_State *L)
{
  struct scheduler *sched = checkudata(L, 1, SCHED_TYPENAME);
  const int task_id = luaL_checkinteger(L, 2);

  if (sched_check_task_id(sched, task_id)) {
    struct sched_task *task = sched_id_to_task(sched, task_id);

    task->terminate = -1;

    if (task->suspended) {
      task->suspended = 0;
      sched_tasklist_add(sched, &sched->active_task_id, task);
    }

    if (task->ev_op) {
      lua_settop(task->co, 3);  /* sched_udata, task_id, evq_udata */
      sys_evq_sched_del(task->co, task->ev_op, task->ev_added);
      task->ev_op = NULL;
      task->ev_added = 0;
      lua_settop(task->co, 0);
    }
  }
  lua_settop(L, 1);
  return 1;
}

/*
 * Arguments: sched_udata
 */
static int
sched_suspend (lua_State *L)
{
  struct scheduler *sched = checkudata(L, 1, SCHED_TYPENAME);
  struct sched_task *task = !sched->ctx ? NULL
   : sched_id_to_task(sched, sched->ctx->task_id);

  if (!task || task->ev_op || task->co != L)
    luaL_argerror(L, 2, "Running coroutine expected");

  task->suspended = -1;

  return lua_yield(L, 0);
}

/*
 * Arguments: sched_udata, task_id, [arguments ...]
 */
static int
sched_resume (lua_State *L)
{
  struct scheduler *sched = checkudata(L, 1, SCHED_TYPENAME);
  const int task_id = luaL_checkinteger(L, 2);
  struct sched_task *task;
  const int narg = lua_gettop(L) - 2;

  task = !sched_check_task_id(sched, task_id) ? NULL
   : sched_id_to_task(sched, task_id);

  if (!task || !task->suspended || task->ev_op)
    luaL_argerror(L, 2, "Suspended coroutine expected");

  sched_tasklist_add(sched, &sched->active_task_id, task);
  task->suspended = 0;

  if (narg) {
    lua_settop(task->co, 0);
    lua_xmove(L, task->co, narg);
  }
  return 0;
}


/*
 * Arguments: sched_udata, evq_udata, arguments ...
 * Returns: [event (string: "r", "w", "t", "e"), eof_status (number)]
 */
static int
sched_event_add (lua_State *L, const int type)
{
  struct scheduler *sched = checkudata(L, 1, SCHED_TYPENAME);
  struct sched_context *sched_ctx = sched->ctx;

  if (!sched_ctx || L != sched_ctx->co)
    luaL_argerror(L, 0, "Scheduler coroutine expected");

  lua_pushvalue(L, 2);  /* evq_udata */
  lua_insert(L, 3);

  lua_pushinteger(L, sched_ctx->task_id);
  lua_insert(L, 2);

  if (!sys_evq_sched_add(L, 4, type)) {
    struct sched_task *task = sched_id_to_task(sched, sched_ctx->task_id);

    task->suspended = -1;

    if (!task->ev_added) {
      task->ev_op = lua_touserdata(L, -1);
      lua_pop(L, 1);  /* pop sync_op_ludata */
    } else if (!task->ev_op) {
      /* error */
      task->ev_added = 0;
      task->suspended = 0;
      return 2; /* nil, error_message */
    }
    return lua_gettop(L);  /* sched_udata, task_id, evq_udata, arguments ... */
  }
  return 2; /* nil, error_message */
}

static int
sched_wait_obj (lua_State *L, const int type, const int cb_idx,
                const int oneshot_idx, const int narg)
{
  lua_settop(L, narg);

  lua_pushthread(L);  /* callback function: coroutine */
  lua_insert(L, cb_idx);

  lua_pushboolean(L, 1);  /* one_shot */
  if (oneshot_idx <= narg + 1)
    lua_insert(L, oneshot_idx);

  return lua_yield(L, sched_event_add(L, type));
}

/*
 * Arguments: sched_udata, evq_udata, obj_udata,
 *	event (string: "r", "w"), [timeout (milliseconds)]
 */
static int
sched_wait_event (lua_State *L)
{
  return sched_wait_obj(L, EVQ_SCHED_OBJ, 5, 7, 5);
}

/*
 * Arguments: sched_udata, evq_udata, timeout (milliseconds)
 */
static int
sched_wait_timer (lua_State *L)
{
  return sched_wait_obj(L, EVQ_SCHED_TIMER, 3, 5, 3);
}

/*
 * Arguments: sched_udata, evq_udata, pid_udata,
 *	[timeout (milliseconds)]
 */
static int
sched_wait_pid (lua_State *L)
{
  return sched_wait_obj(L, EVQ_SCHED_PID, 4, 6, 4);
}

/*
 * Arguments: sched_udata, evq_udata, path (string),
 *	[timeout (milliseconds), modify (boolean)]
 */
static int
sched_wait_dirwatch (lua_State *L)
{
  return sched_wait_obj(L, EVQ_SCHED_DIRWATCH, 4, 6, 5);
}

#ifdef _WIN32
/*
 * Arguments: sched_udata, evq_udata, reg_udata,
 *	[timeout (milliseconds), modify (boolean), recursive (boolean)]
 */
static int
sched_wait_regwatch (lua_State *L)
{
  return sched_wait_obj(L, EVQ_SCHED_REGWATCH, 4, 6, 6);
}
#endif

/*
 * Arguments: sched_udata, evq_udata, signal (string),
 *	[timeout (milliseconds)]
 */
static int
sched_wait_signal (lua_State *L)
{
  return sched_wait_obj(L, EVQ_SCHED_SIGNAL, 4, 6, 4);
}

/*
 * Arguments: sched_udata, evq_udata, sd_udata,
 *	event (string: "r", "w", "accept", "connect"),
 *	[timeout (milliseconds)]
 */
static int
sched_wait_socket (lua_State *L)
{
  return sched_wait_obj(L, EVQ_SCHED_SOCKET, 5, 7, 5);
}

/*
 * Arguments: sched_udata, task_id, evq_udata, ...
 */
void
sys_sched_event_added (lua_State *co, void *ev)
{
  struct scheduler *sched = lua_touserdata(co, 1);
  const int task_id = lua_tointeger(co, 2);
  struct sched_task *task = sched_id_to_task(sched, task_id);

  task->ev_op = ev;
  task->ev_added = -1;

  /* error */
  if (!ev && task->suspended)
    sys_sched_event_ready(co, NULL);
}

/*
 * Arguments: sched_udata, task_id, evq_udata, ...
 */
void
sys_sched_event_ready (lua_State *co, void *ev)
{
  struct scheduler *sched = lua_touserdata(co, 1);
  const int task_id = lua_tointeger(co, 2);
  struct sched_task *task = sched_id_to_task(sched, task_id);

  if (!task || task->ev_op != ev)
    return;

  task->ev_op = NULL;
  task->ev_added = 0;
  task->suspended = 0;

  /* notify */
  if (sched->active_task_id == -1 && sched->nwaiters)
    (void) thread_cond_signal(&sched->cond);

  sched_tasklist_add(sched, &sched->active_task_id, task);

  lua_remove(co, 1);  /* remove sched_udata */
  lua_remove(co, 1);  /* remove task_id */
  lua_remove(co, 1);  /* remove evq_udata */
}


static void
sched_preempt_tasks_hook (lua_State *L, lua_Debug *ar)
{
  (void) ar;

  lua_sethook(L, NULL, 0, 0);
  lua_yield(L, 0);
}

/*
 * Arguments: sched_udata, time_slice (milliseconds)
 */
static int
sched_preempt_tasks (lua_State *L)
{
  struct scheduler *sched = checkudata(L, 1, SCHED_TYPENAME);
  const int msec = lua_tointeger(L, 2);
  thread_critsect_t *csp = &sched->cs;
  unsigned int old_tick = 0;

  sys_vm_leave(L);
  while (!sched->stop) {
    unsigned int tick;

    sys_thread_sleep(msec, 1);  /* wait time slice */

    thread_critsect_enter(csp);
    tick = sched->tick;
    if (tick == old_tick && sched->active_task_id != -1) {
      struct sched_context *sched_ctx = sched->ctx;
      lua_State *co = sched_ctx ? sched_ctx->co : NULL;

      if (co && !lua_gethook(co))
        lua_sethook(co, sched_preempt_tasks_hook, LUA_MASKCOUNT, 1);
    }
    thread_critsect_leave(csp);

    old_tick = tick;
  }
  sys_vm_enter(L);
  return 0;
}

/*
 * Arguments: sched_udata
 * Returns: number
 */
static int
sched_size (lua_State *L)
{
  struct scheduler *sched = checkudata(L, 1, SCHED_TYPENAME);

  lua_pushinteger(L, sched->ntasks);
  return 1;
}

/*
 * Arguments: sched_udata
 * Returns: string
 */
static int
sched_tostring (lua_State *L)
{
  struct scheduler *sched = checkudata(L, 1, SCHED_TYPENAME);

  lua_pushfstring(L, SCHED_TYPENAME " (%p)", sched);
  return 1;
}


#define THREAD_SCHED_METHODS \
  {"scheduler",		sched_self}

#define SCHED_METHODS \
  {"scheduler",		sched_new}

static luaL_Reg sched_meth[] = {
  {"event_queue",	sched_event_queue},
  {"loop",		sched_loop},
  {"stop",		sched_stop},
  {"put",		sched_put},
  {"running",		sched_running},
  {"terminate",		sched_terminate},
  {"suspend",		sched_suspend},
  {"resume",		sched_resume},
  {"wait_event",	sched_wait_event},
  {"wait_timer",	sched_wait_timer},
  {"wait_pid",		sched_wait_pid},
  {"wait_dirwatch",	sched_wait_dirwatch},
#ifdef _WIN32
  {"wait_regwatch",	sched_wait_regwatch},
#endif
  {"wait_signal",	sched_wait_signal},
  {"wait_socket",	sched_wait_socket},
  {"preempt_tasks",	sched_preempt_tasks},
  {"size",		sched_size},
  {"__len",		sched_size},
  {"__tostring",	sched_tostring},
  {"__gc",		sched_close},
  {NULL, NULL}
};
