/* Lua System: Internet Server Application: ISAPI Extension */

#include "../../common.h"

#include <httpext.h>

#ifndef NDEBUG
#include <stdio.h>

static FILE *flog;
#endif


#define LISAPI_DESCR	"Lua ISAPI Extension"

#define LISAPI_POOL_THREADS	8

/* Global Lua State */
static struct {
    lua_State *L;
    struct sys_thread *vmtd;

    int nthreads;
    struct sys_thread *threads[LISAPI_POOL_THREADS];

    char root[MAX_PATHNAME];
} g_ISAPI;


#include "isapi_ecb.c"


static int
traceback (lua_State *L) {
    lua_getfield(L, LUA_GLOBALSINDEX, "debug");
    if (!lua_istable(L, -1)) {
	lua_pop(L, 1);
	return 1;
    }
    lua_getfield(L, -1, "traceback");
    if (!lua_isfunction(L, -1)) {
	lua_pop(L, 2);
	return 1;
    }
    lua_pushvalue(L, 1);  /* pass error message */
    lua_pushinteger(L, 2);  /* skip this function and traceback */
    lua_call(L, 2, 1);  /* call debug.traceback */
    return 1;
}

static int
lisapi_init (void)
{
    lua_State *L;
    char path[MAX_PATHNAME*2];

    if (g_ISAPI.vmtd) return 0;

    L = lua_open();
    if (!L) return -1;

#ifndef NDEBUG
    {
	sprintf(path, "%s\\luaisapi.log", g_ISAPI.root);
	flog = fopen(path, "a");
	if (!flog) goto err_log;
    }
#endif

    luaL_openlibs(L);  /* open standard libraries */

    lua_pushcfunction(L, traceback);  /* push traceback function */

    /* load initialization script */
    {
	sprintf(path, "%s\\isapi.lua", g_ISAPI.root);
	if (luaL_loadfile(L, path))
	    goto err;
    }

    lua_pushstring(L, g_ISAPI.root);
    if (lua_pcall(L, 1, 1, 1)
     || !lua_isfunction(L, -1))
	goto err;

    g_ISAPI.vmtd = sys_get_thread();
    if (g_ISAPI.vmtd) {
	luaL_newmetatable(L, ECB_TYPENAME);
	lua_pushvalue(L, -1);  /* push metatable */
	lua_setfield(L, -2, "__index");  /* metatable.__index = metatable */
	luaL_register(L, NULL, ecb_meth);
	lua_pop(L, 1);

	g_ISAPI.nthreads = 0;
	g_ISAPI.L = L;
	sys_vm_leave();
	sys_set_thread(NULL);
	return 0;
    }
#ifndef NDEBUG
    lua_pushliteral(L, "Threading not initialized");
#endif

 err:
#ifndef NDEBUG
    fprintf(flog, "ERROR: %s\n", lua_tostring(L, -1));
 err_log:
#endif
    lua_close(L);
    return -1;
}

static struct sys_thread *
lisapi_open (LPEXTENSION_CONTROL_BLOCK ecb)
{
    lua_State *L;
    struct sys_thread *td;

    if (g_ISAPI.nthreads) {
	LPEXTENSION_CONTROL_BLOCK *ecbp;

	td = g_ISAPI.threads[--g_ISAPI.nthreads];
	L = sys_lua_tothread(td);

	ecbp = checkudata(L, -1, ECB_TYPENAME);
	*ecbp = ecb;
    }
    else {
	td = sys_new_thread(g_ISAPI.vmtd);
	if (!td) return NULL;

	L = sys_lua_tothread(td);

	lua_pushvalue(g_ISAPI.L, 1);  /* traceback function */
	lua_pushvalue(g_ISAPI.L, 2);  /* process function */
	lua_xmove(g_ISAPI.L, L, 2);  /* move functions to L */

	lua_boxpointer(L, ecb);
	luaL_getmetatable(L, ECB_TYPENAME);
	lua_setmetatable(L, -2);
    }

    sys_set_thread(td);
    return td;
}

static void
lisapi_close (struct sys_thread *td, int status)
{
    if (status || g_ISAPI.nthreads >= LISAPI_POOL_THREADS)
	sys_del_thread(td);
    else
	g_ISAPI.threads[g_ISAPI.nthreads++] = td;

    sys_set_thread(NULL);
}


BOOL WINAPI
DllMain (HANDLE hmodule, DWORD reason, LPVOID reserved)
{
    (void) reserved;

    if (reason == DLL_PROCESS_ATTACH) {
	int n = GetModuleFileNameA(hmodule, g_ISAPI.root, MAX_PATHNAME);
	char *sep;

	if (!n) return FALSE;

	sep = strrchr(g_ISAPI.root, '\\');
	if (sep) *sep = '\0';
    }
    else if (reason == DLL_PROCESS_DETACH)
	TerminateExtension(0);

    return TRUE;
}

BOOL WINAPI
GetExtensionVersion (HSE_VERSION_INFO *ver)
{
    ver->dwExtensionVersion = HSE_VERSION;
    memcpy(ver->lpszExtensionDesc, LISAPI_DESCR, sizeof(LISAPI_DESCR));

    return !lisapi_init();
}

BOOL WINAPI
TerminateExtension (DWORD flags)
{
    (void) flags;

    if (g_ISAPI.vmtd) {
	lua_close(g_ISAPI.L);
	g_ISAPI.vmtd = NULL;

#ifndef NDEBUG
	fclose(flog);
#endif
    }
    return TRUE;
}

DWORD WINAPI
HttpExtensionProc (LPEXTENSION_CONTROL_BLOCK ecb)
{
    lua_State *L;
    struct sys_thread *td;
    int status;

    sys_vm2_enter(g_ISAPI.vmtd);

    td = lisapi_open(ecb);
    if (!td) goto err;

    L = sys_lua_tothread(td);

    lua_pushvalue(L, -2);  /* process function */
    lua_pushvalue(L, -2);  /* ecb_udata */

    ecb->dwHttpStatusCode = 200;
    status = lua_pcall(L, 1, 0, 1);

    if (ecb->dwHttpStatusCode & ~ECB_STATUS_MASK) {
	ecb->dwHttpStatusCode &= ECB_STATUS_MASK;

	lua_pushlightuserdata(L, ecb);
	lua_pushnil(L);
	lua_rawset(L, LUA_REGISTRYINDEX);
    }
    if (status) {
	char *s;
	size_t len;

	lua_pushliteral(L, "\n\n<pre>");
	lua_insert(L, -2);
	lua_concat(L, 2);
	s = (char *) lua_tolstring(L, -1, &len);

	ecb->dwHttpStatusCode = 500;
	ecb->WriteClient(ecb->ConnID, s, (DWORD *) &len, 0);
	lua_pop(L, 1);
    }

    lisapi_close(td, status);
 err:
    sys_vm2_leave(g_ISAPI.vmtd);
    return HSE_STATUS_SUCCESS;
}

