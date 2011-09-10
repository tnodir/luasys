/* Lua System: Threading */

#ifdef _WIN32

#include <process.h>

#define THREAD_FUNC_RES		DWORD
#define THREAD_FUNC_API		THREAD_FUNC_RES WINAPI

typedef unsigned int (WINAPI *thread_func_t) (void *);

typedef unsigned int		thread_id_t;
typedef DWORD 			thread_key_t;

#define thread_getid		GetCurrentThreadId

#else

#define THREAD_FUNC_RES		void *
#define THREAD_FUNC_API		THREAD_FUNC_RES

typedef void *(*thread_func_t) (void *);

typedef pthread_t 		thread_id_t;
typedef pthread_key_t		thread_key_t;

#define thread_getid		pthread_self

#endif /* !WIN32 */


#include "thread_sync.c"


#define THREAD_TYPENAME		"sys.thread"

#define THREAD_XDUP_TAG		"xdup__"

#define THREAD_STACK_SIZE	65536

struct sys_vmthread;

/* Thread's data */
struct sys_thread {
#ifndef _WIN32
    pthread_mutex_t *mutex;
#else
    HANDLE mutex;
    HANDLE th;  /* thread handle */
#endif
    lua_State *L;
    struct sys_vmthread *vmtd;
    thread_id_t tid;
    int cross_vm:	1;  /* fake thread_udata to control VM-thread */
    int daemonized:	1;
    int interrupted:	1;
    int killed:		1;
    int kill_status;
};

/* Main VM-Thread's data */
struct sys_vmthread {
    struct sys_thread td;
#ifndef _WIN32
    pthread_mutex_t vmmutex;
#endif
};

#define INVALID_TLS_INDEX	(thread_key_t) -1

/* Global Thread Local Storage Index */
static thread_key_t g_TLSIndex = INVALID_TLS_INDEX;

#define THREAD_KEY_ADDRESS	(&g_TLSIndex)


static void luaopen_sys_thread (lua_State *L);


void
sys_set_thread (struct sys_thread *td)
{
#ifndef _WIN32
    pthread_setspecific(g_TLSIndex, td);
#else
    TlsSetValue(g_TLSIndex, td);
#endif
}

struct sys_thread *
sys_get_thread (void)
{
    if (g_TLSIndex == INVALID_TLS_INDEX)
	return NULL;
#ifndef _WIN32
    return pthread_getspecific(g_TLSIndex);
#else
    return TlsGetValue(g_TLSIndex);
#endif
}

#define thread_ismain(td)	((td) == (struct sys_thread *) (td)->vmtd)


struct sys_thread *
sys_get_vmthread (struct sys_thread *td)
{
    return td ? (struct sys_thread *) td->vmtd : NULL;
}

struct lua_State *
sys_lua_tothread (struct sys_thread *td)
{
    return td ? td->L : NULL;
}


void
sys_vm2_enter (struct sys_thread *td)
{
    if (td) {
#ifndef _WIN32
	pthread_mutex_lock(td->mutex);
#else
	WaitForSingleObject(td->mutex, INFINITE);
#endif
    }
}

void
sys_vm2_leave (struct sys_thread *td)
{
    if (td) {
#ifndef _WIN32
	pthread_mutex_unlock(td->mutex);
#else
	ReleaseMutex(td->mutex);
#endif
    }
}

void
sys_vm_enter (void)
{
    struct sys_thread *td;

    if (g_TLSIndex == INVALID_TLS_INDEX)
	return;

    td = sys_get_thread();
    sys_vm2_enter(td);

    if (td) {
	if (td->killed) {
	    const int status = td->kill_status;

	    if (thread_ismain(td))
		lua_close(td->L);
	    else {
		td = sys_del_thread(td);
		sys_vm2_leave(td);
	    }
#ifndef _WIN32
	    pthread_exit((THREAD_FUNC_RES) status);
#else
	    _endthreadex((THREAD_FUNC_RES) status);
#endif
	}
	else if (td->interrupted) {
	    lua_pushlightuserdata(td->L, THREAD_KEY_ADDRESS);
	    lua_error(td->L);
	}
    }
}

void
sys_vm_leave (void)
{
    if (g_TLSIndex == INVALID_TLS_INDEX)
	return;

    sys_vm2_leave(sys_get_thread());
}


/*
 * Arguments: ..., thread, thread_udata
 */
static void
thread_settable (lua_State *L, lua_State *NL, thread_id_t tid)
{
    lua_pushlightuserdata(L, THREAD_KEY_ADDRESS);
    lua_rawget(L, LUA_REGISTRYINDEX);

    lua_pushlightuserdata(L, NL);
    lua_pushvalue(L, -4);  /* thread */
    lua_rawset(L, -3);

    lua_pushinteger(L, (lua_Integer) tid);
    lua_pushvalue(L, -3); /* thread_udata */
    lua_rawset(L, -3);

    lua_pop(L, 3);
}

static struct sys_thread *
sys_new_vmthread (lua_State *L)
{
    struct sys_vmthread *vmtd;

    lua_pushthread(L);

    vmtd = lua_newuserdata(L, sizeof(struct sys_vmthread));
    memset(vmtd, 0, sizeof(struct sys_vmthread));
    vmtd->td.L = L;
    vmtd->td.tid = thread_getid();
    vmtd->td.vmtd = vmtd;
    luaL_getmetatable(L, THREAD_TYPENAME);
    lua_setmetatable(L, -2);

#ifndef _WIN32
    if (thread_critsect_new(&vmtd->vmmutex))
	return NULL;
    vmtd->td.mutex = &vmtd->vmmutex;
#else
    vmtd->td.mutex = CreateMutex(NULL, FALSE, NULL);
    if (!vmtd->td.mutex)
	return NULL;
#endif

    return &vmtd->td;
}

struct sys_thread *
sys_new_thread (struct sys_thread *td)
{
    lua_State *NL, *L = td->L;
    struct sys_thread *ntd;

    NL = lua_newthread(L);
    if (!NL) return NULL;

    ntd = lua_newuserdata(L, sizeof(struct sys_thread));
    memset(ntd, 0, sizeof(struct sys_thread));
    ntd->mutex = td->mutex;
    ntd->L = NL;
    ntd->vmtd = td->vmtd;
    ntd->tid = thread_getid();
    luaL_getmetatable(L, THREAD_TYPENAME);
    lua_setmetatable(L, -2);

    thread_settable(L, NL, ntd->tid);  /* save thread to avoid GC */
    return ntd;
}

struct sys_thread *
sys_del_thread (struct sys_thread *td)
{
    lua_State *L = td->L;
    const thread_id_t tid = td->tid;

    td = (struct sys_thread *) td->vmtd;

    /* remove reference to self */
    lua_pushnil(L);
    lua_pushnil(L);
    thread_settable(L, L, tid);

    return td;
}

/*
 * Arguments: thread_udata
 */
static int
thread_done (lua_State *L)
{
    struct sys_thread *td = checkudata(L, 1, THREAD_TYPENAME);

    if (td->mutex) {
	if (thread_ismain(td)) {
#ifndef _WIN32
	    pthread_mutex_destroy(td->mutex);
#else
	    CloseHandle(td->mutex);
#endif
	}
	else {
	    if (!td->killed && !td->daemonized) {
		td->killed = -1;

		sys_vm_leave();
#ifndef _WIN32
		pthread_join(td->tid, NULL);
#else
		WaitForSingleObject(td->th, INFINITE);
#endif
		sys_vm_enter();
	    }

#ifndef _WIN32
	    pthread_detach(td->tid);
#else
	    CloseHandle(td->th);
#endif
	}
	td->mutex = NULL;
    }
    return 0;
}

/*
 * Returns: [boolean]
 */
static int
thread_init (lua_State *L)
{
    struct sys_thread *td;

    /* TLS Index */
    if (g_TLSIndex == INVALID_TLS_INDEX) {
#ifndef _WIN32
	const int res = pthread_key_create(&g_TLSIndex, NULL);
	if (res) {
	    errno = res;
	    goto err;
	}
#else
	if ((g_TLSIndex = TlsAlloc()) == INVALID_TLS_INDEX)
	    goto err;
#endif
    }
    /* VM Mutex */
    td = sys_get_thread();
    if (!td) {
	td = sys_new_vmthread(L);
	if (!td) goto err;

	thread_settable(L, L, td->tid);
	sys_set_thread(td);
	sys_vm2_enter(td);
    }
    lua_pushboolean(L, 1);
    return 1;
 err:
    return sys_seterror(L, 0);
}


static void
thread_setfield (lua_State *L, const char *name, lua_CFunction f)
{
    lua_pushcfunction(L, f);
    lua_setfield(L, -2, name);
}

static void
thread_openlib (lua_State *L, const char *name, lua_CFunction func)
{
    lua_pushcfunction(L, func);
    lua_pushstring(L, name);
    lua_call(L, 1, 0);
}

static void
thread_openlibs (lua_State *L)
{
    thread_openlib(L, "", luaopen_base);
    thread_openlib(L, LUA_LOADLIBNAME, luaopen_package);

    lua_getglobal(L, "package");
    lua_getfield(L, -1, "preload");
    thread_setfield(L, LUA_TABLIBNAME, luaopen_table);
    thread_setfield(L, LUA_IOLIBNAME, luaopen_io);
    thread_setfield(L, LUA_OSLIBNAME, luaopen_os);
    thread_setfield(L, LUA_STRLIBNAME, luaopen_string);
    thread_setfield(L, LUA_MATHLIBNAME, luaopen_math);
    thread_setfield(L, LUA_DBLIBNAME, luaopen_debug);
    lua_pop(L, 2);

    luaopen_sys_thread(L);  /* create table of threads */
}

/*
 * Arguments: function,
 *	[arguments (string | number | boolean | lightuserdata) ...],
 *	thread, thread_udata
 */
static THREAD_FUNC_API
thread_startvm (struct sys_thread *td)
{
    lua_State *L = td->L;
    lua_Integer res;

    thread_settable(L, L, td->tid);

    sys_set_thread(td);
    sys_vm2_enter(td);

    lua_call(L, lua_gettop(L) - 1, 1);
    res = lua_tointeger(L, -1);

    lua_close(L);
    return (THREAD_FUNC_RES) res;
}

/*
 * Arguments: filename (string) | function_dump (string),
 *	[arguments (string | number | boolean | lightuserdata) ...]
 * Returns: [thread_udata]
 */
static int
thread_runvm (lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    struct sys_thread *xtd, *td = sys_get_thread();
    lua_State *NL;
    int res = 0;

    if (!td) luaL_argerror(L, 0, "Threading not initialized");

    /* Fake thread_udata to control VM-thread */
    xtd = lua_newuserdata(L, sizeof(struct sys_thread));
    luaL_getmetatable(L, THREAD_TYPENAME);
    lua_setmetatable(L, -2);

    NL = luaL_newstate();
    if (!NL) goto err;

    thread_openlibs(NL);

    if (path[0] == LUA_SIGNATURE[0]
     ? luaL_loadbuffer(NL, path, lua_rawlen(L, 1), "thread")
     : luaL_loadfile(NL, path)) {
	lua_pushstring(L, lua_tostring(NL, -1));  /* error message */
	lua_close(NL);
	lua_error(L);
    }

    /* Arguments */
    {
	int i, top = lua_gettop(L);

	luaL_checkstack(NL, top + LUA_MINSTACK, "too many arguments");

	for (i = 2; i < top; ++i) {
	    switch (lua_type(L, i)) {
	    case LUA_TSTRING:
		lua_pushstring(NL, lua_tostring(L, i));
		break;
	    case LUA_TNUMBER:
		lua_pushnumber(NL, lua_tonumber(L, i));
		break;
	    case LUA_TBOOLEAN:
		lua_pushboolean(NL, lua_toboolean(L, i));
		break;
	    case LUA_TLIGHTUSERDATA:
		lua_pushlightuserdata(NL, lua_touserdata(L, i));
		break;
	    case LUA_TUSERDATA:
		if (!luaL_getmetafield(L, i, THREAD_XDUP_TAG))
		    luaL_argerror(L, 2, "shareable object expected");
		lua_pushvalue(L, i);
		lua_pushlightuserdata(L, NL);
		lua_call(L, 2, 0);
		break;
	    default:
		luaL_argerror(L, i, "primitive type expected");
	    }
	}
    }

    td = sys_new_vmthread(NL);
    if (!td) goto err_clean;

#ifndef _WIN32
    res = pthread_create(&td->tid, NULL, (thread_func_t) thread_startvm, td);
    if (!res) {
#else
    td->th = (HANDLE) _beginthreadex(NULL, 0,
     (thread_func_t) thread_startvm, td, 0, &td->tid);
    if (td->th) {
#endif
	*xtd = *td;
	xtd->cross_vm = -1;
	return 1;
    }
 err_clean:
    lua_close(NL);
 err:
    return sys_seterror(L, res);
}

/*
 * Arguments: function, [arguments (any) ...]
 */
static THREAD_FUNC_API
thread_start (struct sys_thread *td)
{
    lua_State *L = td->L;
    lua_Integer res;

    sys_set_thread(td);
    sys_vm2_enter(td);

    lua_call(L, lua_gettop(L) - 1, 1);
    res = lua_tointeger(L, -1);

    td = sys_del_thread(td);  /* remove reference to self */
    sys_vm2_leave(td);

    return (THREAD_FUNC_RES) res;
}

/*
 * Arguments: function, [arguments (any) ...]
 * Returns: [thread_udata]
 */
static int
thread_run (lua_State *L)
{
    struct sys_thread *td, *vmtd = sys_get_thread();
    lua_State *NL;
#ifndef _WIN32
    pthread_attr_t attr;
    int res = 0;
#else
    const int res = 0;
#endif

    if (!vmtd) luaL_argerror(L, 0, "Threading not initialized");
    luaL_checktype(L, 1, LUA_TFUNCTION);

    NL = lua_newthread(L);
    if (!NL) goto err;

    td = lua_newuserdata(L, sizeof(struct sys_thread));
    memset(td, 0, sizeof(struct sys_thread));
    td->mutex = vmtd->mutex;
    td->L = NL;
    td->vmtd = vmtd->vmtd;
    luaL_getmetatable(L, THREAD_TYPENAME);
    lua_setmetatable(L, -2);

    lua_pushvalue(L, -1);
    lua_insert(L, 1);  /* thread_udata */

#ifndef _WIN32
    if ((res = pthread_attr_init(&attr))
     || (res = pthread_attr_setstacksize(&attr, THREAD_STACK_SIZE)))
	goto err;

    res = pthread_create(&td->tid, &attr, (thread_func_t) thread_start, td);
    pthread_attr_destroy(&attr);
    if (!res) {
#else
    td->th = (HANDLE) _beginthreadex(NULL, THREAD_STACK_SIZE,
     (thread_func_t) thread_start, td, 0, &td->tid);
    if (td->th) {
#endif
	thread_settable(L, NL, td->tid);  /* save thread to avoid GC */
	lua_xmove(L, NL, lua_gettop(L) - 1);  /* move function and args to new thread */
	return 1;
    }
 err:
    return sys_seterror(L, res);
}

/*
 * Returns: thread_udata, is_main (boolean)
 */
static int
thread_self (lua_State *L)
{
    struct sys_thread *td = sys_get_thread();

    if (!td) luaL_argerror(L, 0, "Threading not initialized");

    lua_pushlightuserdata(L, THREAD_KEY_ADDRESS);
    lua_rawget(L, LUA_REGISTRYINDEX);
    lua_pushinteger(L, (lua_Integer) td->tid);
    lua_rawget(L, -2);
    lua_pushboolean(L, thread_ismain(td));
    return 2;
}

/*
 * Arguments: milliseconds (number), [don't interrupt (boolean)]
 */
static int
thread_sleep (lua_State *L)
{
    const int msec = lua_tointeger(L, 1);
#ifndef _WIN32
    const int not_intr = lua_toboolean(L, 2);
#endif

    sys_vm_leave();
#ifndef _WIN32
    {
	struct timespec req;
	struct timespec *rem = not_intr ? &req : NULL;
	int res;

	req.tv_sec = msec / 1000;
	req.tv_nsec = (msec % 1000) * 1000000;

	do res = nanosleep(&req, rem);
	while (res == -1 && SYS_ERRNO == EINTR && not_intr);
    }
#else
    Sleep(msec);
#endif
    sys_vm_enter();
    return 0;
}

static int
thread_yield (lua_State *L)
{
    (void) L;

    sys_vm_leave();
#ifndef _WIN32
    sched_yield();
#else
    Sleep(0);
#endif
    sys_vm_enter();
    return 0;
}

/*
 * Arguments: thread_udata,
 *	[success/failure (boolean) | status (number)]
 */
static int
thread_kill (lua_State *L)
{
    struct sys_thread *td = checkudata(L, 1, THREAD_TYPENAME);
    const int status = !lua_isboolean(L, 2) ? lua_tointeger(L, 2)
     : (lua_toboolean(L, 2) ? EXIT_SUCCESS : EXIT_FAILURE);

    td->kill_status = status;
    td->killed = -1;

    if (td == sys_get_thread()) {
	thread_yield(L);
    }
#ifndef _WIN32
    else pthread_kill(td->tid, SYS_SIGINTR);
#endif
    return 0;
}

/*
 * Arguments: thread_udata, [recover/interrupt (boolean)]
 */
static int
thread_interrupt (lua_State *L)
{
    struct sys_thread *td = checkudata(L, 1, THREAD_TYPENAME);

    if (lua_toboolean(L, 2)) {
	td->interrupted = 0;
	return 0;
    }

    td->interrupted = -1;
    if (td == sys_get_thread()) {
	lua_pushlightuserdata(L, THREAD_KEY_ADDRESS);
	lua_error(L);
    }
#ifndef _WIN32
    else pthread_kill(td->tid, SYS_SIGINTR);
#endif
    return 0;
}

/*
 * Arguments: thread_udata, [error_object]
 * Returns: boolean
 */
static int
thread_interrupted (lua_State *L)
{
    struct sys_thread *td = checkudata(L, 1, THREAD_TYPENAME);
    void *err_obj = lua_isnoneornil(L, 2) ? THREAD_KEY_ADDRESS
     : lua_touserdata(L, 2);

    lua_pushboolean(L, (td->interrupted && err_obj == THREAD_KEY_ADDRESS));
    return 1;
}

/*
 * Arguments: thread_udata
 * Returns: [thread_udata]
 */
static int
thread_daemonize (lua_State *L)
{
    struct sys_thread *td = checkudata(L, 1, THREAD_TYPENAME);
    int res;

    if (!td->cross_vm)
	luaL_argerror(L, 0, "VM-thread expected");

    lua_settop(L, 1);
    if (td->daemonized) return 1;  /* already a daemon */

#ifndef _WIN32
    res = pthread_detach(td->tid);
#else
    res = !CloseHandle(td->th);
#endif
    if (!res) {
	td->daemonized = -1;
	return 1;
    }
#ifndef _WIN32
    return sys_seterror(L, res);
#else
    return sys_seterror(L, 0);
#endif
}

/*
 * Arguments: thread_udata
 * Returns: number
 */
static int
thread_wait (lua_State *L)
{
    struct sys_thread *td = checkudata(L, 1, THREAD_TYPENAME);
    THREAD_FUNC_RES thread_res;
    int res;

    sys_vm_leave();
#ifndef _WIN32
    res = pthread_join(td->tid, &thread_res);
#else
    res = WaitForSingleObject(td->th, INFINITE) != WAIT_OBJECT_0;
#endif
    sys_vm_enter();

    if (!res) {
#ifdef _WIN32
	GetExitCodeThread(td->th, &thread_res);
#endif
	lua_pushinteger(L, (lua_Integer) thread_res);
	return 1;
    }
#ifndef _WIN32
    return sys_seterror(L, res);
#else
    return sys_seterror(L, 0);
#endif
}

/*
 * Arguments: thread_udata
 * Returns: string
 */
static int
thread_tostring (lua_State *L)
{
    struct sys_thread *td = checkudata(L, 1, THREAD_TYPENAME);

    lua_pushfstring(L, THREAD_TYPENAME " (%p)", (void *) td->tid);
    return 1;
}


#include "thread_dpool.c"
#include "thread_pipe.c"


static luaL_Reg thread_meth[] = {
    {"kill",		thread_kill},
    {"interrupt",	thread_interrupt},
    {"interrupted",	thread_interrupted},
    {"daemonize",	thread_daemonize},
    {"wait",		thread_wait},
    {"__tostring",	thread_tostring},
    {"__gc",		thread_done},
    {NULL, NULL}
};

static luaL_Reg thread_lib[] = {
    {"init",		thread_init},
    {"run",		thread_run},
    {"runvm",		thread_runvm},
    {"self",		thread_self},
    {"sleep",		thread_sleep},
    {"yield",		thread_yield},
    {"data_pool",	dpool_new},
    {"pipe",		pipe_new},
    {NULL, NULL}
};


static void
luaopen_sys_thread (lua_State *L)
{
    /* already initialized? */
    luaL_getmetatable(L, DPOOL_TYPENAME);
    {
	int is_reg = !lua_isnil(L, -1);
	lua_pop(L, 1);
	if (is_reg) return;
    }

    luaL_newmetatable(L, THREAD_TYPENAME);
    lua_pushvalue(L, -1);  /* push metatable */
    lua_setfield(L, -2, "__index");  /* metatable.__index = metatable */
    luaL_register(L, NULL, thread_meth);
    lua_pop(L, 1);

    luaL_newmetatable(L, DPOOL_TYPENAME);
    lua_pushvalue(L, -1);  /* push metatable */
    lua_setfield(L, -2, "__index");  /* metatable.__index = metatable */
    luaL_register(L, NULL, dpool_meth);
    lua_pop(L, 1);

    luaL_newmetatable(L, PIPE_TYPENAME);
    lua_pushvalue(L, -1);  /* push metatable */
    lua_setfield(L, -2, "__index");  /* metatable.__index = metatable */
    luaL_register(L, NULL, pipe_meth);
    lua_pop(L, 1);

    /* create table of threads */
    lua_pushlightuserdata(L, THREAD_KEY_ADDRESS);
    lua_newtable(L);
    lua_rawset(L, LUA_REGISTRYINDEX);

    luaL_register(L, "sys.thread", thread_lib);
    lua_pop(L, 1);
}
