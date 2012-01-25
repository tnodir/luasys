/* Lua System: Threading */

#ifdef _WIN32

#include <process.h>

#define THREAD_FUNC_RES		DWORD
#define THREAD_FUNC_API		THREAD_FUNC_RES WINAPI

typedef DWORD (WINAPI *thread_func_t) (void *);

typedef DWORD 			thread_key_t;

typedef BOOL (WINAPI *PCancelSyncIo) (HANDLE hThread);

static PCancelSyncIo cancelsyncio;

#else

#define THREAD_FUNC_RES		void *
#define THREAD_FUNC_API		THREAD_FUNC_RES

typedef void *(*thread_func_t) (void *);

typedef pthread_key_t		thread_key_t;

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
    pthread_cond_t cond;
#else
    HANDLE mutex;
    HANDLE th;  /* thread handle */
#endif
    lua_State *L;
    struct sys_vmthread *vmtd;
    thread_id_t tid;
    lua_Integer exit_status;
    short volatile interrupted;
    short volatile killed;
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

#define thread_getvm(td)	((struct sys_thread *) (td)->vmtd)
#define thread_isvm(td)		((td) == thread_getvm(td))

static void thread_createmeta (lua_State *L);


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
#ifndef _WIN32
    pthread_mutex_lock(td->mutex);
#else
    WaitForSingleObject(td->mutex, INFINITE);
#endif
}

void
sys_vm2_leave (struct sys_thread *td)
{
#ifndef _WIN32
    pthread_mutex_unlock(td->mutex);
#else
    ReleaseMutex(td->mutex);
#endif
}

void
sys_vm_enter (void)
{
    struct sys_thread *td;

    if (g_TLSIndex == INVALID_TLS_INDEX)
	return;

    td = sys_get_thread();
    if (!td) return;

    sys_vm2_enter(td);

    if (td->killed) {
	THREAD_FUNC_RES res = (THREAD_FUNC_RES) td->exit_status;

	if (thread_isvm(td))
	    lua_close(td->L);
	else {
#ifndef _WIN32
	    pthread_cond_broadcast(&td->cond);
#endif
	    td = sys_del_thread(td);
	    sys_vm2_leave(td);
	}
#ifndef _WIN32
	pthread_exit(res);
#else
	_endthreadex(res);
#endif
    }
    else if (td->interrupted) {
	lua_pushlightuserdata(td->L, THREAD_KEY_ADDRESS);
	lua_error(td->L);
    }
}

void
sys_vm_leave (void)
{
    struct sys_thread *td;

    if (g_TLSIndex == INVALID_TLS_INDEX)
	return;

    td = sys_get_thread();
    if (td) sys_vm2_leave(td);
}

int
sys_isintr (void)
{
#ifndef _WIN32
    if (SYS_ERRNO == EINTR) {
	struct sys_thread *td = sys_get_thread();
	return !(td && (td->interrupted || td->killed));
    }
#endif
    return 0;
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
    vmtd->td.tid = sys_gettid();
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
sys_new_thread (lua_State *L, struct sys_thread *vmtd)
{
    lua_State *NL;
    struct sys_thread *td;
    const int store = !L;

    if (!L) L = vmtd->L;

    NL = lua_newthread(L);
    if (!NL) return NULL;

    td = lua_newuserdata(L, sizeof(struct sys_thread));
    memset(td, 0, sizeof(struct sys_thread));
    td->mutex = vmtd->mutex;
    td->L = NL;
    td->vmtd = vmtd->vmtd;
    td->tid = sys_gettid();
    luaL_getmetatable(L, THREAD_TYPENAME);
    lua_setmetatable(L, -2);

#ifndef _WIN32
    if ((errno = pthread_cond_init(&td->cond, NULL)))
	return NULL;
#endif

    if (store)
	thread_settable(L, NL, td->tid);  /* save thread to avoid GC */
    return td;
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
	if (thread_isvm(td)) {
#ifndef _WIN32
	    pthread_mutex_destroy(td->mutex);
#else
	    CloseHandle(td->mutex);
#endif
	}
#ifndef _WIN32
	pthread_cond_destroy(&td->cond);
#else
	CloseHandle(td->th);
#endif
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


/*
 * Arguments: ..., error_message (string)
 */
static void
thread_abort (lua_State *L)
{
    const char *msg = (lua_type(L, -1) == LUA_TSTRING)
     ? lua_tostring(L, -1) : NULL;

    if (!msg) msg = "(error object is not a string)";
    luai_writestringerror("%s\n", msg);
    abort();
}

#if LUA_VERSION_NUM < 502
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
    thread_openlib(L, "_G", luaopen_base);
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
}
#else
static const luaL_Reg loadedlibs[] = {
    {"_G", luaopen_base},
    {LUA_LOADLIBNAME, luaopen_package},
    {NULL, NULL}
};


static const luaL_Reg preloadedlibs[] = {
    {LUA_COLIBNAME, luaopen_coroutine},
    {LUA_TABLIBNAME, luaopen_table},
    {LUA_IOLIBNAME, luaopen_io},
    {LUA_OSLIBNAME, luaopen_os},
    {LUA_STRLIBNAME, luaopen_string},
    {LUA_BITLIBNAME, luaopen_bit32},
    {LUA_MATHLIBNAME, luaopen_math},
    {LUA_DBLIBNAME, luaopen_debug},
    {NULL, NULL}
};

static void
thread_openlibs (lua_State *L)
{
    const luaL_Reg *lib;
    /* call open functions from 'loadedlibs' and set results to global table */
    for (lib = loadedlibs; lib->func; lib++) {
	luaL_requiref(L, lib->name, lib->func, 1);
	lua_pop(L, 1);  /* remove lib */
    }
    /* add open functions from 'preloadedlibs' into 'package.preload' table */
    luaL_getsubtable(L, LUA_REGISTRYINDEX, "_PRELOAD");
    for (lib = preloadedlibs; lib->func; lib++) {
	lua_pushcfunction(L, lib->func);
	lua_setfield(L, -2, lib->name);
    }
    lua_pop(L, 1);  /* remove _PRELOAD table */
}
#endif

/*
 * Arguments: function,
 *	[arguments (string | number | boolean | lightuserdata) ...],
 *	thread, thread_udata
 */
static THREAD_FUNC_API
thread_startvm (struct sys_thread *td)
{
    lua_State *L = td->L;
    THREAD_FUNC_RES res;

    thread_settable(L, L, td->tid);

    sys_set_thread(td);
    sys_vm2_enter(td);

    if (lua_pcall(L, lua_gettop(L) - 1, 1, 0))
	thread_abort(L);
    if (!td->killed) {
	td->killed = -1;
	td->exit_status = lua_tointeger(L, -1);
    }
    res = (THREAD_FUNC_RES) td->exit_status;

    lua_close(L);
    return res;
}

/*
 * Arguments: filename (string) | function_dump (string),
 *	[arguments (string | number | boolean | lightuserdata | share_object) ...]
 * Returns: [boolean]
 */
static int
thread_runvm (lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    struct sys_thread *td = sys_get_thread();
    lua_State *NL;
#ifndef _WIN32
    pthread_attr_t attr;
#else
    unsigned long hThr;
#endif
    int res = 0;

    if (!td) luaL_argerror(L, 0, "Threading not initialized");

    NL = luaL_newstate();
    if (!NL) goto err;

    thread_openlibs(NL);
    thread_createmeta(NL);

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

	for (i = 2; i <= top; ++i) {
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
    if ((res = pthread_attr_init(&attr)))
	goto err;
    if ((res = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED))) {
	pthread_attr_destroy(&attr);
	goto err;
    }

    res = pthread_create(&td->tid, &attr, (thread_func_t) thread_startvm, td);
    pthread_attr_destroy(&attr);
    if (!res) {
#else
    hThr = _beginthreadex(NULL, 0,
     (thread_func_t) thread_startvm, td, 0, &td->tid);
    if (hThr) {
	td->th = (HANDLE) hThr;
#endif
	lua_pushboolean(L, 1);
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
    THREAD_FUNC_RES res;

    sys_set_thread(td);
    sys_vm2_enter(td);

    if (lua_pcall(L, lua_gettop(L) - 1, 1, 0))
	thread_abort(L);
    if (!td->killed) {
	td->killed = -1;
	td->exit_status = lua_tointeger(L, -1);
    }
    res = (THREAD_FUNC_RES) td->exit_status;

#ifndef _WIN32
    pthread_cond_broadcast(&td->cond);
#endif
    td = sys_del_thread(td);  /* remove reference to self */
    sys_vm2_leave(td);

    return res;
}

/*
 * Arguments: function, [arguments (any) ...]
 * Returns: [thread_udata]
 */
static int
thread_run (lua_State *L)
{
    struct sys_thread *td, *vmtd = sys_get_thread();
#ifndef _WIN32
    pthread_attr_t attr;
    int res = 0;
#else
    unsigned long hThr;
    const int res = 0;
#endif

    if (!vmtd) luaL_argerror(L, 0, "Threading not initialized");
    luaL_checktype(L, 1, LUA_TFUNCTION);

    td = sys_new_thread(L, vmtd);
    if (!td->L) goto err;

    lua_pushvalue(L, -1);
    lua_insert(L, 1);  /* thread_udata */

#ifndef _WIN32
    if ((res = pthread_attr_init(&attr)))
	goto err;
    if ((res = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED))
     || (res = pthread_attr_setstacksize(&attr, THREAD_STACK_SIZE))) {
	pthread_attr_destroy(&attr);
	goto err;
    }

    res = pthread_create(&td->tid, &attr, (thread_func_t) thread_start, td);
    pthread_attr_destroy(&attr);
    if (!res) {
#else
    hThr = _beginthreadex(NULL, THREAD_STACK_SIZE,
     (thread_func_t) thread_start, td, 0, &td->tid);
    if (hThr) {
	td->th = (HANDLE) hThr;
#endif
	thread_settable(L, td->L, td->tid);  /* save thread to avoid GC */
	lua_xmove(L, td->L, lua_gettop(L) - 1);  /* move function and args to new thread */
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
    lua_pushboolean(L, thread_isvm(td));
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
	while (res == -1 && sys_isintr() && not_intr);
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
    const lua_Integer status = !lua_isboolean(L, 2) ? lua_tointeger(L, 2)
     : (lua_toboolean(L, 2) ? EXIT_SUCCESS : EXIT_FAILURE);

    td->exit_status = status;
    td->killed = -1;

    if (td == sys_get_thread()) {
	thread_yield(L);
    } else {
#ifndef _WIN32
	pthread_kill(td->tid, SYS_SIGINTR);
#else
	if (cancelsyncio) cancelsyncio(td->th);
#endif
    }
    return 0;
}

/*
 * Arguments: thread_udata, [recover/interrupt (boolean)]
 */
static int
thread_interrupt (lua_State *L)
{
    struct sys_thread *td = checkudata(L, 1, THREAD_TYPENAME);
    const int recover = lua_toboolean(L, 2);

    if (recover) {
	td->interrupted = 0;
	return 0;
    }

    td->interrupted = -1;
    if (td == sys_get_thread()) {
	lua_pushlightuserdata(L, THREAD_KEY_ADDRESS);
	lua_error(L);
    } else {
#ifndef _WIN32
	pthread_kill(td->tid, SYS_SIGINTR);
#else
	if (cancelsyncio) cancelsyncio(td->th);
#endif
    }
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
    const void *err_obj = lua_isnoneornil(L, 2) ? THREAD_KEY_ADDRESS
     : lua_touserdata(L, 2);

    lua_pushboolean(L, (td->interrupted && err_obj == THREAD_KEY_ADDRESS));
    return 1;
}

/*
 * Arguments: thread_udata, [timeout (milliseconds)]
 * Returns: [status (number) | timeout (false)]
 */
static int
thread_wait (lua_State *L)
{
    struct sys_thread *td = checkudata(L, 1, THREAD_TYPENAME);
    const msec_t timeout = lua_isnoneornil(L, 2)
     ? TIMEOUT_INFINITE : (msec_t) lua_tointeger(L, 2);
    int res = 0;

    if (thread_isvm(td)) luaL_argerror(L, 1, "non VM-thread expected");

    if (!td->killed) {
	sys_vm_leave();
#ifndef _WIN32
	res = thread_cond_wait_impl(&td->cond, td->mutex, &td->killed, 0, timeout);
#else
	res = thread_cond_wait_impl(td->th, timeout);
#endif
	sys_vm_enter();
    }

    if (res >= 0) {
	if (res == 1) {
	    lua_pushboolean(L, 0);
	    return 1;  /* timed out */
	}
	lua_pushinteger(L, td->exit_status);
	return 1;
    }
    return sys_seterror(L, 0);
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
thread_createmeta (lua_State *L)
{
    const struct meta_s {
	const char *tname;
	luaL_Reg *meth;
    } meta[] = {
	{THREAD_TYPENAME,	thread_meth},
	{DPOOL_TYPENAME,	dpool_meth},
	{PIPE_TYPENAME,		pipe_meth},
    };
    int i;

    /* already created? */
    luaL_getmetatable(L, THREAD_TYPENAME);
    {
	const int created = !lua_isnil(L, -1);
	lua_pop(L, 1);
	if (created) return;
    }

    for (i = 0; i < (int) (sizeof(meta) / sizeof(struct meta_s)); ++i) {
	luaL_newmetatable(L, meta[i].tname);
	lua_pushvalue(L, -1);  /* push metatable */
	lua_setfield(L, -2, "__index");  /* metatable.__index = metatable */
	luaL_setfuncs(L, meta[i].meth, 0);
	lua_pop(L, 1);
    }

    /* create threads table */
    lua_pushlightuserdata(L, THREAD_KEY_ADDRESS);
    lua_newtable(L);
    lua_rawset(L, LUA_REGISTRYINDEX);

#ifdef _WIN32
    if (is_WinNT) {
	cancelsyncio = (PCancelSyncIo) GetProcAddress(
	 GetModuleHandleA("kernel32.dll"), "CancelSynchronousIo");
    }
#endif
}

/*
 * Arguments: ..., sys_lib (table)
 */
static void
luaopen_sys_thread (lua_State *L)
{
    luaL_newlib(L, thread_lib);
    lua_setfield(L, -2, "thread");

    thread_createmeta(L);
}
