/* Lua System: Win32 specifics */

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
    size_t max_size = (size_t) lua_tointeger(L, 3);
    const msec_t timeout = lua_isnoneornil(L, 4)
     ? MAILSLOT_WAIT_FOREVER : (msec_t) lua_tointeger(L, 4);

    fd = CreateMailslotA(path, max_size, timeout, NULL);

    if (fd != (fd_t) -1) {
	*fdp = fd;
	lua_settop(L, 1);
	return 1;
    }
    return sys_seterror(L, 0);
}

/*
 * Arguments: fd_udata
 * Returns: [next_message_size (number), message_count (number)]
 */
static int
win32_mailslot_info (lua_State *L)
{
    fd_t fd = (fd_t) lua_unboxinteger(L, 1, FD_TYPENAME);
    DWORD next_size, count;

    if (GetMailslotInfo(fd, NULL, &next_size, &count, NULL)) {
	if (next_size == MAILSLOT_NO_MESSAGE)
	    next_size = count = 0;
	lua_pushinteger(L, next_size);
	lua_pushinteger(L, count);
	return 2;
    }
    return sys_seterror(L, 0);
}

/*
 * Arguments: [File_API (string: "OEM", "ANSI")]
 * Returns: File_API (string)
 */
static int
win32_file_apis (lua_State *L)
{
    const char *api = lua_tostring(L, 1);

    lua_pushstring(L, AreFileApisANSI() ? "ANSI" : "OEM");
    if (api) {
	if (*api == 'O')
	    SetFileApisToOEM();
	else
	    SetFileApisToANSI();
    }
    return 1;
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
    {"mailslot",	win32_mailslot}, \
    {"mailslot_info",	win32_mailslot_info}

static luaL_reg win32_lib[] = {
    {"file_apis",	win32_file_apis},
    {"beep",		win32_beep},
    {"registry",	reg_new},
    {"utf8_console",	utf8_console},
    {NULL, NULL}
};


static void
luaopen_sys_win32 (lua_State *L)
{
    luaL_newmetatable(L, WREG_TYPENAME);
    lua_pushvalue(L, -1);  /* push metatable */
    lua_setfield(L, -2, "__index");  /* metatable.__index = metatable */
    luaL_register(L, NULL, reg_meth);

    luaL_register(L, "sys.win32", win32_lib);
    lua_pop(L, 2);

    luaopen_sys_win32_service(L);
}
