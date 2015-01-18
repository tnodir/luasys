/* Lua System: Win32 specifics */

PCancelSynchronousIo pCancelSynchronousIo;
PCancelIoEx pCancelIoEx;

CRITICAL_SECTION g_CritSect;
static int volatile g_CritSectInit = 0;

#if !(defined(_WIN32_WCE) || defined(WIN32_VISTA))
int is_WinNT;
#endif


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

/*
 * Arguments: drive_letter (string: A: .. Z:)
 * Returns: [dos_device_name (string)]
 */
static int
win32_drive_dosname (lua_State *L)
{
  const char *drive = luaL_checkstring(L, 1);
  char buf[MAX_PATHNAME];
  const int res = QueryDosDeviceA(drive, buf, MAX_PATHNAME);

  if (res > 2) {
    lua_pushlstring(L, buf, res - 2);
    return 1;
  }
  return sys_seterror(L, 0);
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
  {"drive_dosname",	win32_drive_dosname},
  {"registry",		reg_new},
  {NULL, NULL}
};


static void
win32_init (lua_State *L)
{
  (void) L;

#ifndef _WIN32_WCE
  /* Is Win32 NT platform? */
  {
    OSVERSIONINFO osvi;

    memset(&osvi, 0, sizeof(OSVERSIONINFO));
    osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
    GetVersionEx(&osvi);

#ifndef WIN32_VISTA
    is_WinNT = (osvi.dwPlatformId == VER_PLATFORM_WIN32_NT);
#else
    if (osvi.dwPlatformId != VER_PLATFORM_WIN32_NT
     || osvi.dwMajorVersion < 6)
      luaL_argerror(L, 0, "Windows Vista+ expected");
#endif
  }
#endif

  if (is_WinNT) {
    HANDLE mh = GetModuleHandleA("kernel32.dll");

    pCancelSynchronousIo = (PCancelSynchronousIo)
     GetProcAddress(mh, "CancelSynchronousIo");
    pCancelIoEx = (PCancelIoEx)
     GetProcAddress(mh, "CancelIoEx");
  }

  /* Initialize global critical section */
  if (!g_CritSectInit) {
    g_CritSectInit = 1;
    InitCriticalSection(&g_CritSect);
  }
}

/*
 * Arguments: ..., sys_lib (table)
 */
static void
luaopen_sys_win32 (lua_State *L)
{
  win32_init(L);

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
