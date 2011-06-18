/* Lua System: Logging */

#ifndef _WIN32
#include <syslog.h>
#endif

#define LOG_TYPENAME	"sys.log"

/* Log event types */
#ifdef _WIN32
#define LOG_TDEBUG	EVENTLOG_SUCCESS
#define LOG_TERROR	EVENTLOG_ERROR_TYPE
#define LOG_TWARN	EVENTLOG_WARNING_TYPE
#define LOG_TINFO	EVENTLOG_INFORMATION_TYPE
#else
#define LOG_TDEBUG	LOG_DEBUG
#define LOG_TERROR	LOG_ERR
#define LOG_TWARN	LOG_WARNING
#define LOG_TINFO	LOG_INFO
#endif

struct sys_log {
#ifdef _WIN32
    HANDLE h;
#endif
    int type;
};


/*
 * Arguments: [ident (string)]
 * Returns: [log_udata]
 */
static int
sys_log (lua_State *L)
{
    const char *ident = luaL_optstring(L, 1, "lua");
    struct sys_log *logp = lua_newuserdata(L, sizeof(struct sys_log));

#ifndef _WIN32
    openlog(ident, LOG_CONS, LOG_USER);
    {
#else
    /* register the event source */
    {
	HKEY app_hk = NULL, ident_hk = NULL;
	DWORD opt;

	RegOpenKeyExA(HKEY_LOCAL_MACHINE,
	 "System\\CurrentControlSet\\Services\\EventLog\\Application",
	 0, KEY_WRITE, &app_hk);

	RegCreateKeyExA(app_hk, ident,
	 0, NULL, 0, KEY_WRITE, NULL, &ident_hk, &opt);

	if (ident_hk && opt == REG_CREATED_NEW_KEY) {
	    RegSetValueExA(ident_hk, "EventMessageFile",
	     0, REG_EXPAND_SZ,
	     (const unsigned char *) "%SystemRoot%\\System32\\netmsg.dll",
	     sizeof("%SystemRoot%\\System32\\netmsg.dll"));

	    opt = 1;
	    RegSetValueExA(ident_hk, "TypesSupported",
	     0, REG_DWORD, (unsigned char *) &opt, sizeof(DWORD));
	}
	RegCloseKey(ident_hk);
	RegCloseKey(app_hk);
    }

    logp->h = OpenEventLogA(NULL, ident);
    if (logp->h) {
#endif
	logp->type = LOG_TERROR;
	luaL_getmetatable(L, LOG_TYPENAME);
	lua_setmetatable(L, -2);
	return 1;
    }
#ifdef _WIN32
    return sys_seterror(L, 0);
#endif
}

/*
 * Arguments: log_udata
 */
static int
log_close (lua_State *L)
{
#ifndef _WIN32
    (void) L;
    closelog();
#else
    struct sys_log *logp = checkudata(L, 1, LOG_TYPENAME);
    CloseEventLog(logp->h);
#endif
    return 0;
}

/*
 * Arguments: log_udata, type (string: "debug", "error", "warn", "info")
 * Returns: log_report (function)
 */
static int
log_type (lua_State *L)
{
    struct sys_log *logp = checkudata(L, 1, LOG_TYPENAME);
    const char *type = lua_tostring(L, 2);

    if (type) {
	int t = LOG_TERROR;
	switch (type[0]) {
	case 'd': t = LOG_TDEBUG; break;
	case 'e': t = LOG_TERROR; break;
	case 'w': t = LOG_TWARN; break;
	case 'i': t = LOG_TINFO; break;
	default: luaL_argerror(L, 2, "invalid option");
	}
	logp->type = t;
    }
    return luaL_getmetafield(L, 1, "__call");
}

/*
 * Arguments: log_udata, message (string)
 * Returns: [log_udata]
 */
static int
log_report (lua_State *L)
{
    struct sys_log *logp = checkudata(L, 1, LOG_TYPENAME);
    const char *msg = luaL_checkstring(L, 2);

#ifndef _WIN32
    sys_vm_leave();
    syslog(logp->type, "%s", msg);
    sys_vm_enter();
#else
    WCHAR *buf[9];

    memset(buf, 0, sizeof(buf));
    buf[0] = utf8_to_filename(msg);
    if (!buf[0])
	return sys_seterror(L, ERROR_NOT_ENOUGH_MEMORY);

    sys_vm_leave();
    ReportEventW(logp->h, (short) logp->type,
     0, 3299, NULL, sizeof(buf) / sizeof(buf[0]), 0,
     (const WCHAR **) buf, NULL);
    sys_vm_enter();

    free(buf[0]);
#endif
    lua_settop(L, 1);
    return 1;
}


#define LOG_METHODS \
    {"log",		sys_log}

static luaL_reg log_meth[] = {
    {"__index",		log_type},
    {"__call",		log_report},
    {"__gc",		log_close},
    {NULL, NULL}
};
