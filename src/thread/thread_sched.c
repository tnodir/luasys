/* Lua System: Threading: Scheduler */

#define SCHED_TYPENAME	"sys.thread.scheduler"

/* Scheduler environ. table reserved indexes */
#define SCHED_ENV_CORO		1  /* coroutine */
#define SCHED_ENV_TASKS		2  /* start of coroutine tasks */

/* Scheduler coroutine reserved indexes */
#define SCHED_CORO_ENV		1  /* environ. */
#define SCHED_CORO_EVQ		2  /* evq_udata */
#define SCHED_CORO_CBEND	3  /* callback: on_end */

struct sched_task {
    lua_State *co;  /* coroutine */

    struct sched_task *prev, *next;  /* chained list of tasks */
    void *ev;  /* waiting in event queue */

#define SCHED_TASK_STARTED	0x01  /* execution was started */
#define SCHED_TASK_KILLED	0x02  /* termination requested */
    unsigned int flags;
};

struct scheduler {
    lua_State *L;  /* storage */

    struct sched_task *run_task;  /* circular list of active tasks */
    struct sched_task *wait_task;  /* circular list of event waiting tasks */

#define SCHED_CALLBACK_END	0x01  /* end callback exists */
    unsigned int flags;

    thread_event_t tev;  /* notification */
};


#define sched_tasklist_add(head_taskp,task) \
    do { \
	struct sched_task *head_task = *head_taskp; \
	if (head_task) { \
	    task->prev = head_task->prev; \
	    task->prev->next = task; \
	    head_task->prev = task; \
	    task->next = head_task; \
	} else { \
	    task->prev = task->next = task; \
	} \
	*head_taskp = task; \
    } while (0)

#define sched_tasklist_del(head_taskp,task) \
    do { \
	if (task != task->next) { \
	    task->prev->next = task->next; \
	    task->next->prev = task->prev; \
	} else { \
	    *head_taskp = NULL; \
	} \
    } while (0)



static struct sched_task *
sched_task_coro (struct scheduler *sched, lua_State *co)
{
    lua_State *L = sched->L;
    struct sched_task *task;

    lua_rawgetp(L, SCHED_CORO_ENV, co);
    task = lua_touserdata(L, -1);
    lua_pop(L, 1);
    return task;
}

static void
sched_task_del (lua_State *L, struct scheduler *sched,
                struct sched_task *task, const int error)
{
    lua_State *NL = sched->L;
    lua_State *co = task->co;
    struct sched_task **list = task->ev
     ? &sched->wait_task : &sched->run_task;
    const int call_end = (sched->flags & SCHED_CALLBACK_END);

    sched_tasklist_del(list, task);

    if (task->ev) {
	void *ev = task->ev;
	task->ev = NULL;

	lua_pushvalue(sched->L, SCHED_CORO_EVQ);
	lua_xmove(sched->L, L, 1);  /* evq_udata */
	sys_evq_del(L, ev);
    }

    if (call_end) {
	lua_pushvalue(sched->L, SCHED_CORO_CBEND);
	lua_xmove(sched->L, L, 1);  /* function */
	lua_pushthread(co);
	lua_xmove(co, L, 1);  /* coroutine */
	lua_settop(co, 1);  /* task_udata */
	if (error) lua_xmove(co, L, 1);  /* error message */
    }

    /* remove coroutine from scheduler */
    lua_pushnil(NL);
    lua_pushnil(NL);
    lua_rawsetp(NL, SCHED_CORO_ENV, co);  /* coroutine_ludata -> task_ludata */
    lua_rawsetp(NL, SCHED_CORO_ENV, task);  /* task_ludata -> coroutine */

    if (call_end) {
	lua_call(L, (error ? 2 : 1), 0);
    }
}

/*
 * Returns: [coroutine]
 */
static int
sched_coroutine (lua_State *L)
{
    lua_State *co = lua_newthread(L);

    if (!co) return 0;

    lua_newuserdata(co, sizeof(struct sched_task));
    return 1;
}

/*
 * Arguments: evq_udata, [end_callback (function)]
 * Returns: [sched_udata]
 */
static int
sched_new (lua_State *L)
{
    struct scheduler *sched;
    lua_State *NL;

    (void) checkudata(L, 1, EVQ_TYPENAME);

#undef ARG_LAST
#define ARG_LAST	2

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

    sched->L = NL;
    lua_rawseti(L, -2, SCHED_ENV_CORO);  /* save coroutine to avoid GC */

    lua_pushvalue(L, 1);  /* evq_udata (SCHED_CORO_EVQ) */
    if (lua_isfunction(L, 2)) {
	lua_pushvalue(L, 2);  /* end_callback (SCHED_CORO_CBEND) */
	lua_xmove(L, NL, 3);
	sched->flags |= SCHED_CALLBACK_END;
    } else {
	lua_xmove(L, NL, 2);
    }

    if (!thread_event_new(&sched->tev)) {
	return 1;
    }
    return sys_seterror(L, 0);
}

/*
 * Arguments: sched_udata
 */
static int
sched_close (lua_State *L)
{
    struct scheduler *sched = checkudata(L, 1, SCHED_TYPENAME);

    thread_event_del(&sched->tev);
    return 0;
}

/*
 * Arguments: sched_udata, coroutine, function, [arguments ...]
 * Returns: sched_udata
 */
static int
sched_put (lua_State *L)
{
    struct scheduler *sched = checkudata(L, 1, SCHED_TYPENAME);
    lua_State *co = lua_tothread(L, 2);
    const int nmove = lua_gettop(L) - 2;
    lua_State *NL = sched->L;
    struct sched_task *task;

    if (!co) luaL_checktype(L, 2, LUA_TTHREAD);

    task = lua_touserdata(co, 1);
    if (!task || lua_gettop(co) != 1
     || lua_rawlen(co, 1) != sizeof(struct sched_task))
	luaL_argerror(L, 2, "sys.thread.coroutine expected");

    memset(task, 0, sizeof(struct sched_task));
    task->co = co;

    luaL_checkstack(co, nmove, NULL);
    lua_xmove(L, co, nmove);

    /* store coroutine in scheduler */
    lua_pushthread(co);
    lua_xmove(co, NL, 1);
    lua_rawsetp(NL, SCHED_CORO_ENV, task);  /* task_ludata -> coroutine */
    lua_pushlightuserdata(NL, task);
    lua_rawsetp(NL, SCHED_CORO_ENV, co);  /* coroutine_ludata -> task_ludata */

    sched_tasklist_add(&sched->run_task, task);
    task->flags = 0;

    lua_settop(L, 1);
    return 1;
}

/*
 * Arguments: sched_udata, coroutine
 * Returns: sched_udata
 */
static int
sched_kill (lua_State *L)
{
    struct scheduler *sched = checkudata(L, 1, SCHED_TYPENAME);
    lua_State *co = lua_tothread(L, 2);
    struct sched_task *task;

    if (!co) luaL_checktype(L, 2, LUA_TTHREAD);

    task = sched_task_coro(sched, co);
    if (task) {
	task->flags |= SCHED_TASK_KILLED;
    }

    lua_settop(L, 1);
    return 1;
}


/*
 * Arguments: sched_udata, [timeout (milliseconds)]
 * Returns: [sched_udata | timedout (false)]
 */
static int
sched_loop (lua_State *L)
{
    struct sys_thread *td = sys_thread_get();
    struct scheduler *sched = checkudata(L, 1, SCHED_TYPENAME);
    const msec_t timeout = lua_isnoneornil(L, 2)
     ? TIMEOUT_INFINITE : (msec_t) lua_tointeger(L, 2);

    if (!td) luaL_argerror(L, 0, "Threading not initialized");

    for (; ; ) {
	struct sched_task *task;
	lua_State *co;
	int narg;

	for (; ; ) {
	    int res;

	    task = sched->run_task;
	    if (task) break;

	    res = thread_event_wait(&sched->tev, td, timeout);
	    sys_thread_check(td);
	    if (res) {
		if (res == 1) {
		    lua_pushboolean(L, 0);
		    return 1;  /* timed out */
		}
		return sys_seterror(L, 0);
	    }
	}
	sched->run_task = task->next;

	co = task->co;
	narg = lua_gettop(co) - 1;  /* - task_udata */

	if (!(task->flags & SCHED_TASK_STARTED)) {
	    task->flags |= SCHED_TASK_STARTED;
	    --narg;  /* - function */
	}

	switch (lua_resume(co, L, narg)) {
	case LUA_YIELD:
	    if (!(task->flags & SCHED_TASK_KILLED))
		break;
	    /* FALLTHROUGH */
	case 0:
	    sched_task_del(L, sched, task, 0);
	    break;
	default:
	    sched_task_del(L, sched, task, 1);
	}
    }

    lua_settop(L, 1);
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


/*
 * Arguments: obj_udata, arguments ...
 */
int
sys_sched_eagain (lua_State *L, lua_CFunction func,
                  int async_flags, int *nresult)
{
    struct sys_thread *td = sys_thread_get();

    if (td && L == td->sched_task) {
	struct scheduler *sched = td->sched;
	const int nres = lua_gettop(L) + 2;
	void *ev;

	lua_assert(sched);

	luaL_checkstack(L, nres, NULL);

	lua_pushvalue(sched->L, SCHED_CORO_EVQ);
	lua_xmove(sched->L, L, 1);  /* evq_udata */
	lua_pushvalue(L, 1);  /* obj_udata */

	ev = sys_evq_add(L, async_flags);
	if (ev) {
	    struct sched_task *task = sched_task_coro(sched, L);

	    sched_tasklist_del(&sched->run_task, task);
	    sched_tasklist_add(&sched->wait_task, task);
	    task->ev = ev;

	    lua_pushcfunction(L, func);
	    lua_pushlightuserdata(L, sched);
	    *nresult = nres;
	} else {
	    *nresult = -1;
	}
	return 1;
    }
    return 0;
}

int
sys_sched_ready (lua_State *L, lua_State *co)
{
    struct scheduler *sched = lua_touserdata(co, -1);
    struct sched_task *task = sched_task_coro(sched, co);
    int i;

    if (!task) return 0;

    /* call waiting task's function */
    {
	const int top = lua_gettop(L);
	const int co_top = lua_gettop(co);
	const int narg = co_top - 2;

	lua_pushvalue(co, co_top - 1);  /* function */
	for (i = 1; i <= narg; ++i)
	    lua_pushvalue(co, i);  /* arguments */
	lua_xmove(co, L, 1 + narg);
	lua_call(L, narg, LUA_MULTRET);

	if (lua_isboolean(L, top + 1) && !lua_toboolean(L, top + 1)) {
	    /* eagain: continue waiting the event */
	    lua_settop(L, top);
	    lua_settop(co, co_top);
	    return 1;
	}

	/* move results to task's coroutine */
	lua_settop(co, 0);
	lua_xmove(L, co, lua_gettop(L) - top);
    }

    /* notify */
    if (!sched->run_task)
	thread_event_signal(&sched->tev);

    /* activate the task */
    sched_tasklist_del(&sched->wait_task, task);
    sched_tasklist_add(&sched->run_task, task);
    task->ev = NULL;
    return 0;
}


#define SCHED_METHODS \
    {"coroutine",	sched_coroutine}, \
    {"scheduler",	sched_new}

static luaL_Reg sched_meth[] = {
    {"put",		sched_put},
    {"kill",		sched_kill},
    {"loop",		sched_loop},
    {"__tostring",	sched_tostring},
    {"__gc",		sched_close},
    {NULL, NULL}
};
