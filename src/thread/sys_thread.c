/* Lua System: Threading */

#ifdef _WIN32

#include <process.h>

#define thread_getid		GetCurrentThreadId

#define THREAD_FUNC_API		unsigned int WINAPI

typedef unsigned int (WINAPI *thread_func_t) (void *);

typedef unsigned int		thread_id_t;
typedef DWORD 			thread_key_t;

#else

#include <pthread.h>

#define thread_getid		pthread_self

#define THREAD_FUNC_API		void *

typedef void *(*thread_func_t) (void *);

typedef pthread_t 		thread_id_t;
typedef pthread_key_t		thread_key_t;

#endif /* !WIN32 */


#include "thread_sync.c"


#define THREAD_STACK_SIZE	65536

struct sys_vmthread;

/* Thread's data */
struct sys_thread {
#ifndef _WIN32
    pthread_mutex_t *mutex;
#else
    HANDLE mutex;
#endif
    lua_State *L;
    struct sys_vmthread *vmtd;  /* vm-thread */
    thread_id_t tid;
    int volatile interrupted;  /* thread interrupted? */
    sys_trigger_t trigger;  /* notify event_queue about termination or vm-i/o */
};

/* Buffer for messages */
struct thread_msg_buf {
    char *ptr;
    int len;
    int idx, top;  /* buffer indexes */
    int nmsg;  /* number of messages */
};

/* Main vm-thread's data */
struct sys_vmthread {
    struct sys_thread td;

#ifndef _WIN32
    pthread_mutex_t vmmutex;
#endif

#ifdef _WIN32
    thread_critsect_t bufcs;  /* guard access to buffer */
#endif
    thread_event_t bufev;
    struct thread_msg_buf volatile buffer;
};

#define INVALID_TLS_INDEX	(thread_key_t) -1

/* Global Thread Local Storage Index */
static thread_key_t g_TLSIndex = INVALID_TLS_INDEX;


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

    if (td && td->interrupted) {
	lua_pushlightuserdata(td->L, &g_TLSIndex);
	lua_error(td->L);
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
    lua_pushlightuserdata(L, &g_TLSIndex);
    lua_rawget(L, LUA_REGISTRYINDEX);

    lua_pushlightuserdata(L, NL);
    lua_pushvalue(L, -4);  /* thread */
    lua_rawset(L, -3);

    lua_pushnumber(L, (unsigned long) tid);
    lua_pushvalue(L, -3); /* thread_udata */
    lua_rawset(L, -3);

    lua_pop(L, 3);
}


struct sys_thread *
sys_new_thread (struct sys_thread *td)
{
    lua_State *L = td->L;
    lua_State *NL;
    struct sys_thread *ntd;

    NL = lua_newthread(L);
    if (!NL) return NULL;

    ntd = lua_newuserdata(L, sizeof(struct sys_thread));
    memset(ntd, 0, sizeof(struct sys_thread));
    ntd->mutex = td->mutex;
    ntd->L = NL;
    ntd->vmtd = td->vmtd;
    ntd->tid = thread_getid();

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


static int
vmthread_new (lua_State *L, struct sys_vmthread **vmtdp)
{
    struct sys_vmthread *vmtd;

    lua_pushthread(L);

    vmtd = lua_newuserdata(L, sizeof(struct sys_vmthread));
    memset(vmtd, 0, sizeof(struct sys_vmthread));
    vmtd->td.L = L;
    vmtd->td.tid = thread_getid();
    vmtd->td.vmtd = vmtd;

    /* vm-mutex destructor */
    lua_pushlightuserdata(L, &g_TLSIndex);
    lua_rawget(L, LUA_REGISTRYINDEX);
    lua_pushvalue(L, -1);
    lua_setmetatable(L, -3);
    lua_pushlightuserdata(L, vmtd);
    lua_pushvalue(L, -3);  /* thread_udata */
    lua_rawset(L, -3);
    lua_pop(L, 1);

    if (thread_event_new(&vmtd->bufev))
	return -1;

#ifndef _WIN32
    vmtd->td.mutex = &vmtd->vmmutex;
    if (thread_critsect_new(vmtd->td.mutex)) {
	thread_event_del(&vmtd->bufev);
	return -1;
    }
#else
    if (thread_critsect_new(&vmtd->bufcs)) {
	thread_event_del(&vmtd->bufev);
	return -1;
    }

    vmtd->td.mutex = CreateMutex(NULL, FALSE, NULL);
    if (!vmtd->td.mutex) {
	thread_event_del(&vmtd->bufev);
	thread_critsect_del(&vmtd->bufcs);
	return -1;
    }
#endif

    *vmtdp = vmtd;
    return 0;
}

static int
vmthread_del (lua_State *L)
{
    struct sys_vmthread *vmtd = lua_touserdata(L, 1);

    if (vmtd->td.mutex) {
	thread_event_del(&vmtd->bufev);
#ifdef _WIN32
	thread_critsect_del(&vmtd->bufcs);
#endif
	free(vmtd->buffer.ptr);

#ifndef _WIN32
	pthread_mutex_destroy(vmtd->td.mutex);
#else
	CloseHandle(vmtd->td.mutex);
#endif
	vmtd->td.mutex = NULL;
    }
    return 0;
}


/*
 * Returns: [thread_ludata]
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
	if (vmthread_new(L, (void *) &td))
	    goto err;

	thread_settable(L, L, td->tid);
	sys_set_thread(td);
	sys_vm2_enter(td);
    }
    lua_pushlightuserdata(L, td->vmtd);
    return 1;
 err:
    return sys_seterror(L, 0);
}

/*
 * Arguments: function, [arguments (any) ...]
 */
static THREAD_FUNC_API
thread_start (struct sys_thread *td)
{
    lua_State *L = td->L;

    sys_set_thread(td);
    sys_vm2_enter(td);

    if (lua_pcall(L, lua_gettop(L) - 1, 0, 0)) {
	if (td->interrupted && lua_touserdata(L, -1) == &g_TLSIndex)
	    lua_pop(L, 1);
	else
	    lua_error(L);
    }

    /* notify event_queue */
    if (td->trigger)
	sys_trigger_notify(&td->trigger, SYS_EVEOF | SYS_EVDEL);

    /* remove reference to self */
    td = sys_del_thread(td);

    sys_vm2_leave(td);
    return 0;
}

/*
 * Arguments: function, [arguments (any) ...]
 * Returns: [thread_ludata]
 */
static int
thread_run (lua_State *L)
{
    struct sys_thread *vmtd = sys_get_thread();
    lua_State *NL;
    struct sys_thread *td;
#ifndef _WIN32
    pthread_attr_t attr;
    int res = 0;
#else
    HANDLE hThr;
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

#ifndef _WIN32
    if ((res = pthread_attr_init(&attr))
     || (res = pthread_attr_setstacksize(&attr, THREAD_STACK_SIZE)))
	goto err;
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    res = pthread_create(&td->tid, &attr,
     (thread_func_t) thread_start, td);
    pthread_attr_destroy(&attr);
    if (!res) {
#else
    hThr = (HANDLE) _beginthreadex(NULL, THREAD_STACK_SIZE,
     (thread_func_t) thread_start, td, 0, &td->tid);
    if (hThr) {
	CloseHandle(hThr);
#endif
	thread_settable(L, NL, td->tid);  /* save thread to avoid GC */
	lua_xmove(L, NL, lua_gettop(L));  /* move function and args to NL */

	lua_pushlightuserdata(L, td);
	return 1;
    }
 err:
    return sys_seterror(L, res);
}


/*
 * Arguments: function, master (thread_ludata),
 *	[arguments (string | number | boolean | lightuserdata) ...],
 *	thread, thread_udata
 */
static THREAD_FUNC_API
thread_startvm (struct sys_thread *td)
{
    lua_State *L = td->L;

    thread_settable(L, L, td->tid);

    sys_set_thread(td);
    sys_vm2_enter(td);

    if (lua_pcall(L, lua_gettop(L) - 1, 0, 0)) {
	if (td->interrupted && lua_touserdata(L, -1) == &g_TLSIndex)
	    lua_pop(L, 1);
	else
	    lua_error(L);
    }

    /* notify event_queue */
    if (td->trigger)
	sys_trigger_notify(&td->trigger, SYS_EVEOF | SYS_EVDEL);

    lua_close(L);
    return 0;
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
 * Arguments: filename (string) | function_dump (string),
 *	[arguments (string | number | boolean | lightuserdata) ...]
 * Returns: [thread_ludata]
 */
static int
thread_runvm (lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    lua_State *NL = NULL;
    struct sys_vmthread *vmtd = (struct sys_vmthread *) sys_get_thread();
#ifndef _WIN32
    pthread_attr_t attr;
#else
    HANDLE hThr;
#endif
    int res = 0;

    if (!vmtd) luaL_argerror(L, 0, "Threading not initialized");

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
    lua_pushlightuserdata(NL, vmtd);  /* master */
    {
	int i, top = lua_gettop(L);

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
	    case LUA_TUSERDATA:
		lua_pushlightuserdata(NL, lua_touserdata(L, i));
		break;
	    default:
		luaL_argerror(L, i, "primitive type expected");
	    }
	}
    }

    if (vmthread_new(NL, &vmtd))
	goto err_clean;

#ifndef _WIN32
    res = pthread_attr_init(&attr);
    if (res) goto err_clean;
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    res = pthread_create(&vmtd->td.tid, &attr,
     (thread_func_t) thread_startvm, vmtd);
    pthread_attr_destroy(&attr);
    if (!res) {
#else
    hThr = (HANDLE) _beginthreadex(NULL, 0,
     (thread_func_t) thread_startvm, vmtd, 0, &vmtd->td.tid);
    if (hThr) {
	CloseHandle(hThr);
#endif
	lua_pushlightuserdata(L, vmtd);
	return 1;
    }
 err_clean:
    lua_close(NL);
 err:
    return sys_seterror(L, res);
}

/*
 * Arguments: thread_ludata
 */
static int
thread_interrupt (lua_State *L)
{
    struct sys_thread *td = lua_touserdata(L, 1);

    td->interrupted = 1;
#ifndef _WIN32
    pthread_kill(td->tid, SYS_SIGINTR);
#endif
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

/*
 * Returns: [thread_ludata, main thread_ludata]
 */
static int
thread_self (lua_State *L)
{
    struct sys_thread *td = sys_get_thread();

    if (!td) return 0;

    lua_pushlightuserdata(L, td);
    lua_pushlightuserdata(L, td->vmtd);
    return 2;
}

/*
 * Arguments: ..., thread_ludata
 */
static sys_trigger_t *
thread_get_trigger (lua_State *L, struct sys_vmthread **vmtdp)
{
    struct sys_thread *td = lua_touserdata(L, -1);

    *vmtdp = td->vmtd;
    return &td->trigger;
}


#include "thread_dpool.c"
#include "thread_msg.c"


static luaL_reg thread_lib[] = {
    {"init",		thread_init},
    {"run",		thread_run},
    {"runvm",		thread_runvm},
    {"interrupt",	thread_interrupt},
    {"yield",		thread_yield},
    {"sleep",		thread_sleep},
    {"self",		thread_self},
    {"data_pool",	thread_data_pool},
    {"msg_send",	thread_msg_send},
    {"msg_recv",	thread_msg_recv},
    {"msg_count",	thread_msg_count},
    {NULL, NULL}
};


static void
luaopen_sys_thread (lua_State *L)
{
    luaL_newmetatable(L, DPOOL_TYPENAME);
    lua_pushvalue(L, -1);  /* push metatable */
    lua_setfield(L, -2, "__index");  /* metatable.__index = metatable */
    luaL_register(L, NULL, dpool_meth);
    lua_pushcfunction(L, (lua_CFunction) dpool_get_trigger);
    lua_setfield(L, -2, SYS_TRIGGER_TAG);
    lua_pop(L, 1);

    /* create table of threads */
    lua_pushlightuserdata(L, &g_TLSIndex);
    lua_newtable(L);
    lua_pushliteral(L, "__gc");  /* mutex destructor */
    lua_pushcfunction(L, vmthread_del);
    lua_rawset(L, -3);
    lua_rawset(L, LUA_REGISTRYINDEX);

    luaL_register(L, "sys.thread", thread_lib);
    lua_pushcfunction(L, (lua_CFunction) thread_get_trigger);
    lua_setfield(L, -2, SYS_TRIGGER_TAG);
    lua_pop(L, 1);
}
