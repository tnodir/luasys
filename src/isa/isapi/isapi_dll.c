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

static void lisapi_createmeta (lua_State *L);


#include "isapi_ecb.c"


static int
traceback (lua_State *L) {
#if LUA_VERSION_NUM < 502
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
#else
  const char *msg = lua_tostring(L, 1);
  if (msg)
    luaL_traceback(L, L, msg, 1);
  else if (!lua_isnoneornil(L, 1)) {  /* is there an error object? */
    if (!luaL_callmeta(L, 1, "__tostring"))  /* try its 'tostring' metamethod */
      lua_pushliteral(L, "(no error message)");
  }
  return 1;
#endif
}

static int
lisapi_init (void)
{
  lua_State *L;
  char path[MAX_PATHNAME*2];

  if (g_ISAPI.vmtd) return 0;

  L = luaL_newstate();
  if (!L) return -1;

#ifndef NDEBUG
  {
    sprintf(path, "%s\\luaisapi.log", g_ISAPI.root);
    flog = fopen(path, "a");
    if (!flog) goto err_log;
  }
#endif

  luaL_openlibs(L);  /* open standard libraries */

  lua_pushcfunction(L, traceback);  /* 1: traceback function */

  /* load initialization script */
  {
    sprintf(path, "%s\\isapi.lua", g_ISAPI.root);
    if (luaL_loadfile(L, path))
      goto err;
  }

  lua_pushstring(L, g_ISAPI.root);
  if (lua_pcall(L, 1, 2, 1)
   || !lua_isfunction(L, 2)  /* 2: request handler */
   || !lua_isfunction(L, 3))  /* 3: closing handler */
    goto err;

  g_ISAPI.vmtd = sys_thread_get();
  if (g_ISAPI.vmtd) {
    lisapi_createmeta(L);

    g_ISAPI.nthreads = 0;
    g_ISAPI.L = L;
    sys_vm_leave(L);
    sys_thread_set(NULL);
    return 0;
  }
#ifndef NDEBUG
  lua_pushliteral(L, "Threading not initialized");
#endif

 err:
#ifndef NDEBUG
  fprintf(flog, "init: %s\n", lua_tostring(L, -1));
  fclose(flog);
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
    L = sys_thread_tolua(td);

    ecbp = checkudata(L, -1, ECB_TYPENAME);
    *ecbp = ecb;
  } else {
    td = sys_thread_new(g_ISAPI.L, g_ISAPI.vmtd, NULL, 0);
    if (!td) return NULL;

    L = sys_thread_tolua(td);

    lua_pushvalue(g_ISAPI.L, 1);  /* traceback function */
    lua_pushvalue(g_ISAPI.L, 2);  /* process function */
    lua_xmove(g_ISAPI.L, L, 2);  /* move functions to L */

    lua_boxpointer(L, ecb);
    luaL_getmetatable(L, ECB_TYPENAME);
    lua_setmetatable(L, -2);
  }

  sys_thread_set(td);
  return td;
}

static void
lisapi_close (struct sys_thread *td, int status)
{
  if (status || g_ISAPI.nthreads >= LISAPI_POOL_THREADS)
    sys_thread_del(td);
  else
    g_ISAPI.threads[g_ISAPI.nthreads++] = td;

  sys_thread_set(NULL);
}


BOOL WINAPI DllMain (HANDLE hmodule, DWORD reason, LPVOID reserved);

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
  } else if (reason == DLL_PROCESS_DETACH)
    TerminateExtension(0);

  return TRUE;
}

BOOL WINAPI
GetExtensionVersion (HSE_VERSION_INFO *ver)
{
  ver->dwExtensionVersion = MAKELONG(HSE_VERSION_MINOR, HSE_VERSION_MAJOR);
  memcpy(ver->lpszExtensionDesc, LISAPI_DESCR, sizeof(LISAPI_DESCR));

  return !lisapi_init();
}

BOOL WINAPI
TerminateExtension (DWORD flags)
{
  (void) flags;

  if (g_ISAPI.vmtd) {
    sys_vm2_enter(g_ISAPI.vmtd);
    sys_thread_set(g_ISAPI.vmtd);

    if (lua_pcall(g_ISAPI.L, 0, 0, 1)) {
#ifndef NDEBUG
      fprintf(flog, "close: %s\n", lua_tostring(g_ISAPI.L, -1));
#endif
    }
    lua_close(g_ISAPI.L);

#ifndef NDEBUG
    fclose(flog);
#endif
    g_ISAPI.vmtd = NULL;
  }
  return TRUE;
}

DWORD WINAPI
HttpExtensionProc (LPEXTENSION_CONTROL_BLOCK ecb)
{
  lua_State *L;
  struct sys_thread *td;
  DWORD res = HSE_STATUS_SUCCESS;
  int status;

  sys_vm2_enter(g_ISAPI.vmtd);

  td = lisapi_open(ecb);
  if (!td) goto err;

  L = sys_thread_tolua(td);

  lua_pushvalue(L, -2);  /* process function */
  lua_pushvalue(L, -2);  /* ecb_udata */

  ecb->dwHttpStatusCode = 200;
  status = lua_pcall(L, 1, 0, 1);

  if (ecb->dwHttpStatusCode & ECB_STATUS_PENDING) {
    ecb->dwHttpStatusCode &= ~ECB_STATUS_PENDING;
    res = HSE_STATUS_PENDING;
  } else {
    if (ecb->dwHttpStatusCode & ~ECB_STATUS_MASK) {
      ecb->dwHttpStatusCode &= ECB_STATUS_MASK;

      lua_pushnil(L);
      lua_rawsetp(L, LUA_REGISTRYINDEX, ecb);
    }
    if (status) {
      const char *s;
      size_t len;
      union sys_rwptr s_ptr;  /* to avoid "const cast" warning */

      lua_pushliteral(L, "\n\n<pre>");
      lua_insert(L, -2);
      lua_concat(L, 2);
      s = lua_tolstring(L, -1, &len);

#ifndef NDEBUG
      fprintf(flog, "process: %s\n", s);
#endif

      ecb->dwHttpStatusCode = 500;
      s_ptr.r = s;
      ecb->WriteClient(ecb->ConnID, s_ptr.w, (DWORD *) &len, 0);
      lua_pop(L, 1);
    }
  }

  lisapi_close(td, status);
 err:
  sys_vm2_leave(g_ISAPI.vmtd);
  return res;
}


static luaL_Reg isapi_lib[] = {
  {"ecb",	ecb_new},
  {NULL, NULL}
};


static void
lisapi_createmeta (lua_State *L)
{
  /* already created? */
  luaL_getmetatable(L, ECB_TYPENAME);
  {
    const int created = !lua_isnil(L, -1);
    lua_pop(L, 1);
    if (created) return;
  }

  luaL_newmetatable(L, ECB_TYPENAME);
  lua_pushvalue(L, -1);  /* push metatable */
  lua_setfield(L, -2, "__index");  /* metatable.__index = metatable */
  luaL_setfuncs(L, ecb_meth, 0);
  lua_pop(L, 1);
}

LUALIB_API int
luaopen_sys_isapi (lua_State *L)
{
  luaL_register(L, LUA_ISAPILIBNAME, isapi_lib);
  lisapi_createmeta(L);
  return 1;
}
