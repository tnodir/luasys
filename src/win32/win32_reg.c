/* Lua System: Win32 specifics: Registry */

#define WREG_TYPENAME	"sys.win32.registry"

#define REG_KEY_SZ	256
#define REG_DATA_SZ	16384


/*
 * Returns: reg_udata
 */
static int
reg_new (lua_State *L)
{
    lua_boxpointer(L, NULL);
    luaL_getmetatable(L, WREG_TYPENAME);
    lua_setmetatable(L, -2);
    return 1;
}

/*
 * Arguments: ..., [root (reg_udata | string)]
 */
static HKEY
reg_root2key (lua_State *L, int idx)
{
    static const HKEY key_flags[] = {
	HKEY_CLASSES_ROOT, HKEY_CURRENT_USER,
	HKEY_LOCAL_MACHINE, HKEY_USERS
    };
    static const char *const key_names[] = {
	"HKEY_CLASSES_ROOT", "HKEY_CURRENT_USER",
	"HKEY_LOCAL_MACHINE", "HKEY_USERS", NULL
    };
    HKEY hk;

    if (lua_isuserdata(L, idx))
	hk = lua_unboxpointer(L, idx, WREG_TYPENAME);
    else
	hk = key_flags[luaL_checkoption(L, idx, "HKEY_CURRENT_USER", key_names)];
    return hk;
}

/*
 * Arguments: ..., [mode (string: "r", "w", "rw")]
 */
static REGSAM
reg_mode2sam (lua_State *L, int idx)
{
    const char *mode = lua_tostring(L, idx);
    if (mode) {
	if (mode[0] == 'w') return KEY_WRITE;
	if (mode[1] == 'w') return KEY_ALL_ACCESS;
    }
    return KEY_READ;
}

/*
 * Arguments: reg_udata, [root (reg_udata | string), subkey (string),
 *	mode (string: "r", "w", "rw")]
 * Returns: [reg_udata]
 */
static int
reg_open (lua_State *L)
{
    HKEY *hkp = checkudata(L, 1, WREG_TYPENAME);
    HKEY hk = reg_root2key(L, 2);
    const char *subkey = lua_tostring(L, 3);
    REGSAM desired = reg_mode2sam(L, 4);
    int res;

    res = RegOpenKeyExA(hk, subkey, 0, desired, hkp);
    if (!res) {
	lua_settop(L, 1);
	return 1;
    }
    return sys_seterror(L, res);
}

/*
 * Arguments: reg_udata, root (reg_udata | string), subkey (string),
 *	[class (string), option (string: "non_volatile", "volatile"),
 *	mode (string: "r", "w", "rw")]
 * Returns: [reg_udata, opened_existing (boolean)]
 */
static int
reg_create (lua_State *L)
{
    HKEY *hkp = checkudata(L, 1, WREG_TYPENAME);
    HKEY hk = reg_root2key(L, 2);
    char *subkey = (char *) luaL_checkstring(L, 3);
    char *class = (char *) lua_tostring(L, 4);
    const char *s = lua_tostring(L, 5);
    DWORD opt = (s && *s == 'v') ? REG_OPTION_VOLATILE : 0;
    REGSAM desired = reg_mode2sam(L, 6);
    int res;

    res = RegCreateKeyExA(hk, subkey, 0, class, opt, desired, NULL, hkp, &opt);
    if (!res) {
	lua_settop(L, 1);
	lua_pushboolean(L, opt == REG_OPENED_EXISTING_KEY);
	return 2;
    }
    return sys_seterror(L, res);
}

/*
 * Arguments: reg_udata
 * Returns: boolean
 */
static int
reg_close (lua_State *L)
{
    HKEY *hkp = checkudata(L, 1, WREG_TYPENAME);
    if (*hkp) {
	lua_pushboolean(L, !RegCloseKey(*hkp));
	*hkp = NULL;
	return 1;
    }
    return 0;
}

/*
 * Arguments: reg_udata, subkey (string)
 * Returns: [reg_udata]
 */
static int
reg_del_key (lua_State *L)
{
    HKEY hk = lua_unboxpointer(L, 1, WREG_TYPENAME);
    const char *subkey = luaL_checkstring(L, 2);
    int res;

    res = RegDeleteKeyA(hk, subkey);
    if (!res) {
	lua_settop(L, 1);
	return 1;
    }
    return sys_seterror(L, res);
}

/*
 * Arguments: reg_udata, [name (string)]
 * Returns: [reg_udata]
 */
static int
reg_del_value (lua_State *L)
{
    HKEY hk = lua_unboxpointer(L, 1, WREG_TYPENAME);
    const char *name = lua_tostring(L, 2);
    int res;

    res = RegDeleteValueA(hk, name);
    if (!res) {
	lua_settop(L, 1);
	return 1;
    }
    return sys_seterror(L, res);
}

/*
 * Arguments: reg_udata, [index (number)]
 * Returns: [index (number), name (string), class (string)]
 */
static int
reg_keys (lua_State *L)
{
    if (lua_gettop(L) == 1) {  /* `for' start? */
	/* return generator (this function), state (reg_udata), */
	lua_pushinteger(L, 0);  /* and initial value */
	return 3;
    } else {  /* `for' step */
	HKEY hk = lua_unboxpointer(L, 1, WREG_TYPENAME);
	int i = lua_tointeger(L, 2);
	char name[REG_KEY_SZ], class[REG_KEY_SZ];
	DWORD cname = REG_KEY_SZ, cclass = REG_KEY_SZ;
	int res;

	res = RegEnumKeyExA(hk, i, name, &cname, 0, class, &cclass, NULL);
	if (!res) {
	    lua_pushinteger(L, ++i);  /* next value */
	    lua_pushlstring(L, name, cname);
	    lua_pushlstring(L, class, cclass);
	    return 3;
	}
	return (res == ERROR_NO_MORE_ITEMS) ? 0 : sys_seterror(L, res);
    }
}


static int
reg_pushvalue (lua_State *L, DWORD type, char *data, DWORD cdata)
{
    if (type == REG_DWORD)
	lua_pushnumber(L, *((unsigned long *) data));
    else if (type == REG_EXPAND_SZ) {
	char buf[REG_DATA_SZ];

	cdata = ExpandEnvironmentStringsA(data, buf, sizeof(buf));
	if (!cdata) return 0;

	lua_pushlstring(L, buf, cdata - 1);
    } else
	lua_pushlstring(L, data, cdata - (type == REG_SZ ? 1 : 0));
    return 1;
}

/*
 * Arguments: reg_udata, [index (number)]
 * Returns: [index (number), name (string), data (number | string)]
 */
static int
reg_values (lua_State *L)
{
    if (lua_gettop(L) == 1) {  /* `for' start? */
	/* return generator (this function), state (reg_udata), */
	lua_pushinteger(L, 0);  /* and initial value */
	return 3;
    } else {  /* `for' step */
	HKEY hk = lua_unboxpointer(L, 1, WREG_TYPENAME);
	int i = lua_tointeger(L, 2);
	char name[REG_KEY_SZ], data[REG_DATA_SZ];
	DWORD cname = REG_KEY_SZ, cdata = REG_DATA_SZ;
	DWORD type;
	int res;

	res = RegEnumValueA(hk, i, name, &cname, NULL, &type,
	 (unsigned char *) data, &cdata);
	if (!res) {
	    lua_pushinteger(L, ++i);  /* next value */
	    lua_pushlstring(L, name, cname);
	    if (reg_pushvalue(L, type, data, cdata))
		return 3;
	}
	return (res == ERROR_NO_MORE_ITEMS) ? 0 : sys_seterror(L, res);
    }
}

/*
 * Arguments: reg_udata, [name (string)]
 * Returns: [data (string)]
 */
static int
reg_get (lua_State *L)
{
    HKEY hk = lua_unboxpointer(L, 1, WREG_TYPENAME);
    const char *name = lua_tostring(L, 2);
    char data[REG_DATA_SZ];
    DWORD cdata = REG_DATA_SZ;
    DWORD type;
    int res;

    res = RegQueryValueExA(hk, name, NULL, &type,
     (unsigned char *) data, &cdata);
    if (!res && reg_pushvalue(L, type, data, cdata)) {
	return 1;
    }
    return sys_seterror(L, res);
}

/*
 * Arguments: reg_udata, name (string), data (number | string)
 * Returns: [reg_udata]
 */
static int
reg_set (lua_State *L)
{
    HKEY hk = lua_unboxpointer(L, 1, WREG_TYPENAME);
    const char *name = lua_tostring(L, 2);
    const unsigned char *data;
    DWORD data_num, cdata, type, res;

    if (lua_type(L, 3) == LUA_TSTRING) {
	size_t len;

	type = REG_SZ;
	data = (const unsigned char *) lua_tolstring(L, 3, &len);
	cdata = len + 1;  /* + terminating null character */
    } else {
	type = REG_DWORD;
	data_num = lua_tointeger(L, 3);
	data = (const unsigned char *) &data_num;
	cdata = sizeof(DWORD);
    }
    res = RegSetValueExA(hk, name, 0, type, data, cdata);
    if (!res) {
	lua_settop(L, 1);
	return 1;
    }
    return sys_seterror(L, res);
}

/*
 * Arguments: reg_udata
 * Returns: string
 */
static int
reg_tostring (lua_State *L)
{
    HKEY hk = lua_unboxpointer(L, 1, WREG_TYPENAME);
    lua_pushfstring(L, WREG_TYPENAME " (%p)", hk);
    return 1;
}


static luaL_reg reg_meth[] = {
    {"open",		reg_open},
    {"create",		reg_create},
    {"close",		reg_close},
    {"del_key",		reg_del_key},
    {"del_value",	reg_del_value},
    {"keys",		reg_keys},
    {"values",		reg_values},
    {"get",		reg_get},
    {"set",		reg_set},
    {"__tostring",	reg_tostring},
    {"__gc",		reg_close},
    {NULL, NULL}
};
