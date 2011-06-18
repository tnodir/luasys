#ifndef LUASYS_H
#define LUASYS_H

#define LUA_SYSLIBNAME	"sys"
LUALIB_API int luaopen_sys (lua_State *L);

#define LUA_SOCKLIBNAME	"sys.sock"
LUALIB_API int luaopen_sys_sock (lua_State *L);

#endif
