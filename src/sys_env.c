/* Lua System: Process Environment */

#ifndef _WIN32
extern char **environ;
#endif


/*
 * Arguments: name (string)
 * Returns: [value (string)]
 */
static int
sys_getenv (lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);

#ifndef _WIN32
    lua_pushstring(L, getenv(name));
    return 1;
#else
    unsigned int len;

    len = GetEnvironmentVariableA(name, NULL, 0);
    if (len) {
	char *buf = malloc(len);
	if (buf && GetEnvironmentVariableA(name, buf, len) == len - 1) {
	    lua_pushlstring(L, buf, len - 1);
	    free(buf);
	    return 1;
	}
	free(buf);
    }
    return sys_seterror(L, 0);
#endif
}

/*
 * Arguments: name (string), [value (string)]
 * Returns: [boolean]
 */
static int
sys_setenv (lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    const char *value = lua_tostring(L, 2);

#ifndef _WIN32
    if (!(value ? setenv(name, value, 1)
#if defined(__linux__)
     : unsetenv(name))) {
#else
     : ((void) unsetenv(name), 0))) {
#endif
#else
    if (SetEnvironmentVariableA(name, value)) {
#endif
	lua_pushboolean(L, 1);
	return 1;
    }
    return sys_seterror(L, 0);
}

/*
 * Returns: environment (table)
 */
static int
sys_env (lua_State *L)
{
    const char *name, *value, *end;
#ifndef _WIN32
    const char **env = (const char **) environ;
#else
    char *env = GetEnvironmentStringsA();
#endif

    if (!env) return 0;
    lua_newtable(L);
#ifndef _WIN32
    for (; (name = *env); ++env) {
#else
    for (name = env; *name != '\0'; name = end + 1) {
#endif
	value = strchr(name, '=') + 1;
	end = strchr(value, '\0');

	lua_pushlstring(L, name, value - name - 1);
	lua_pushlstring(L, value, end - value);
	lua_rawset(L, -3);
    }
#ifdef _WIN32
    FreeEnvironmentStringsA(env);
#endif
    return 1;
}


#define ENV_METHODS \
    {"getenv",		sys_getenv}, \
    {"setenv",		sys_setenv}, \
    {"env",		sys_env}
