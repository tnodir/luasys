/* Lua System: Unix specifics */

/*
 * Arguments: path (string)
 * Returns: [boolean]
 */
static int
sys_chroot (lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);

    if (!chroot(path)) {
	lua_pushboolean(L, 1);
	return 1;
    }
    return sys_seterror(L, 0);
}

/*
 * Returns: [boolean]
 */
static int
sys_daemonize (lua_State *L)
{
    lua_pushboolean(L, 1);
    if (getppid() == 1)
	return 1;  /* already a daemon */

    switch (fork()) {
    case -1: goto err;
    case 0:  break;
    default: _exit(0);
    }
    if (setsid() == -1 || chdir("/") == -1)
	goto err;
    umask(0);
    /* standard files */
    {
	int fd = open("/dev/null", O_RDWR, 0);
	dup2(fd, 0);
	dup2(fd, 1);
	dup2(fd, 2);
	if (fd > 2) close(fd);
    }
    return 1;
 err:
    return sys_seterror(L, 0);
}

/*
 * Arguments: path (string), [permissions (number)]
 * Returns: [boolean]
 */
static int
sys_mkfifo (lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    mode_t perm = (mode_t) luaL_optinteger(L, 2, SYS_FILE_PERMISSIONS);

    if (!mkfifo(path, perm)) {
	lua_pushboolean(L, 1);
	return 1;
    }
    return sys_seterror(L, 0);
}


#define UNIX_METHODS \
    {"chroot",		sys_chroot}, \
    {"daemonize",	sys_daemonize}, \
    {"mkfifo",		sys_mkfifo}
