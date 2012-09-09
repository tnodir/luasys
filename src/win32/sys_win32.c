/* Lua System: Win32 specifics */

PCancelSynchronousIo pCancelSynchronousIo;
PCancelIoEx pCancelIoEx;
PGetQueuedCompletionStatusEx pGetQueuedCompletionStatusEx;
PSetFileCompletionNotificationModes pSetFileCompletionNotificationModes;

int is_WinNT;


/*
 * Arguments: fd_udata, path (string),
 *	[maximum_message_size (number), timeout (milliseconds)]
 * Returns: [fd_udata]
 */
static int
win32_mailslot (lua_State *L)
{
  fd_t fd, *fdp = checkudata(L, 1, FD_TYPENAME);
  const char *path = luaL_checkstring(L, 2);
  const DWORD max_size = lua_tointeger(L, 3);
  const DWORD timeout = luaL_optinteger(L, 4, MAILSLOT_WAIT_FOREVER);

  fd = CreateMailslotA(path, max_size, timeout, NULL);

  if (fd != (fd_t) -1) {
    *fdp = fd;
    lua_settop(L, 1);
    return 1;
  }
  return sys_seterror(L, 0);
}

/*
 * Arguments: fd_udata, [timeout (milliseconds)]
 * Returns: [fd_udata]
 */
static int
win32_set_mailslot_info (lua_State *L)
{
  fd_t fd = (fd_t) lua_unboxinteger(L, 1, FD_TYPENAME);
  const DWORD timeout = luaL_optinteger(L, 2, MAILSLOT_WAIT_FOREVER);

  if (SetMailslotInfo(fd, timeout)) {
    lua_settop(L, 1);
    return 1;
  }
  return sys_seterror(L, 0);
}

/*
 * Arguments: fd_udata
 * Returns: [next_message_size (number), message_count (number),
 *	timeout (milliseconds)]
 */
static int
win32_get_mailslot_info (lua_State *L)
{
  fd_t fd = (fd_t) lua_unboxinteger(L, 1, FD_TYPENAME);
  DWORD next_size, count, timeout;

  if (GetMailslotInfo(fd, NULL, &next_size, &count, &timeout)) {
    if (next_size == MAILSLOT_NO_MESSAGE)
      next_size = count = 0;
    lua_pushinteger(L, next_size);
    lua_pushinteger(L, count);
    lua_pushinteger(L, timeout);
    return 3;
  }
  return sys_seterror(L, 0);
}

/*
 * Arguments: [frequency (hertz), duration (milliseconds)]
 */
static int
win32_beep (lua_State *L)
{
  const int freq = luaL_optinteger(L, 1, 1000);
  const int dur = luaL_optinteger(L, 2, 100);

  Beep(freq, dur);
  return 0;
}


#include "win32_reg.c"
#include "win32_svc.c"
#include "win32_utf8.c"


#define WIN32_METHODS \
  {"mailslot",		win32_mailslot}, \
  {"set_mailslot_info",	win32_set_mailslot_info}, \
  {"get_mailslot_info",	win32_get_mailslot_info}

static luaL_Reg win32_lib[] = {
  {"beep",		win32_beep},
  {"registry",	reg_new},
  {NULL, NULL}
};


static void
win32_init (void)
{
#ifdef _WIN32_WCE
  is_WinNT = 1;
#else
  /* Is Win32 NT platform? */
  {
    OSVERSIONINFO osvi;

    osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
    is_WinNT = (GetVersionEx(&osvi)
     && osvi.dwPlatformId == VER_PLATFORM_WIN32_NT);
  }
#endif

  if (is_WinNT) {
    HANDLE mh = GetModuleHandleA("kernel32.dll");

    pCancelSynchronousIo = (PCancelSynchronousIo)
     GetProcAddress(mh, "CancelSynchronousIo");
    pCancelIoEx = (PCancelIoEx)
     GetProcAddress(mh, "CancelIoEx");
    pGetQueuedCompletionStatusEx = (PGetQueuedCompletionStatusEx)
     GetProcAddress(mh,"GetQueuedCompletionStatusEx");
    pSetFileCompletionNotificationModes = (PSetFileCompletionNotificationModes)
     GetProcAddress(mh,"SetFileCompletionNotificationModes");
  }
}

/*
 * Arguments: ..., sys_lib (table)
 */
static void
luaopen_sys_win32 (lua_State *L)
{
  win32_init();

  luaL_newlib(L, win32_lib);
  lua_pushvalue(L, -1);  /* push win32_lib */
  lua_setfield(L, -3, "win32");

  luaopen_sys_win32_service(L);
  lua_pop(L, 1);  /* pop win32_lib */

  luaL_newmetatable(L, WREG_TYPENAME);
  lua_pushvalue(L, -1);  /* push metatable */
  lua_setfield(L, -2, "__index");  /* metatable.__index = metatable */
  luaL_setfuncs(L, reg_meth, 0);
  lua_pop(L, 1);
}
