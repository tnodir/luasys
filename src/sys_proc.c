/* Lua System: Processes */

#ifdef _WIN32
#include <shellapi.h>	/* ShellExecute */
#endif

#define PID_TYPENAME	"sys.pid"

#ifdef _POSIX_OPEN_MAX
#define MAX_OPEN_FDS	_POSIX_OPEN_MAX
#else
#define MAX_OPEN_FDS	20
#endif

struct sys_pid {
    int id;
#ifdef _WIN32
    HANDLE h;
#endif
};


/*
 * Arguments: [command (string)]
 * Returns: [boolean]
 */
static int
sys_run (lua_State *L)
{
    const char *cmd = luaL_checkstring(L, 1);
    int res;

#ifndef _WIN32
    sys_vm_leave();
    res = system(cmd);
    sys_vm_enter();

    if (res != -1) {
#else
    char path[MAX_PATHNAME];
    const char *arg = NULL;

    arg = strchr(cmd, ' ');
    if (arg) {
	unsigned int n = arg - cmd;
	if (n >= sizeof(path))
	    return 0;
	memcpy(path, cmd, n);
	path[n] = '\0';
	cmd = path;
	++arg;  /* skip space */
    }

    sys_vm_leave();
    res = (int) ShellExecuteA(NULL, NULL, cmd, arg, NULL, 0);
    sys_vm_enter();

    if (res > 32) {
#endif
	lua_pushboolean(L, 1);
	return 1;
    }
    return sys_seterror(L, 0);
}

#define MAX_ARGS	32
/*
 * Arguments: filename (string), [arguments (table: {number => string}),
 *	pid_udata (of new process),
 *	in (fd_udata), out (fd_udata), err (fd_udata),
 *	is_console (boolean)]
 * Returns: [boolean]
 */
static int
sys_spawn (lua_State *L)
{
    const char *cmd = luaL_checkstring(L, 1);
    struct sys_pid *pidp = lua_isuserdata(L, 3) ? checkudata(L, 3, PID_TYPENAME) : NULL;
    fd_t *in_fdp = lua_isuserdata(L, 4) ? checkudata(L, 4, FD_TYPENAME) : NULL;
    fd_t *out_fdp = lua_isuserdata(L, 5) ? checkudata(L, 5, FD_TYPENAME) : NULL;
    fd_t *err_fdp = lua_isuserdata(L, 6) ? checkudata(L, 6, FD_TYPENAME) : NULL;

#ifndef _WIN32
    const char *argv[MAX_ARGS];
    int pid;

    /* fill arguments array */
    {
	const char *arg;
	int i = 1;

	/* filename in argv[0] */
	if ((arg = strrchr(cmd, '/')))
	    argv[0] = arg + 1;
	else
	    argv[0] = cmd;
	if (lua_istable(L, 2))
	    do {
		lua_rawgeti(L, 2, i);
		arg = lua_tostring(L, -1);
		lua_pop(L, 1);
		if (!arg) break;
		argv[i] = arg;
	    } while (++i < MAX_ARGS);
	argv[i] = NULL;
    }

    switch ((pid = fork())) {
    case 0:
	/* restore sigpipe */
	signal_set(SIGPIPE, SIG_DFL);
	/* redirect standard handles */
	if (in_fdp) dup2(*in_fdp, STDIN_FILENO);
	if (out_fdp) dup2(*out_fdp, STDOUT_FILENO);
	if (err_fdp) dup2(*err_fdp, STDERR_FILENO);
	/* close other files */
	{
	    int n = sysconf(_SC_OPEN_MAX);
	    if (n < 0) n = MAX_OPEN_FDS;
	    while (n > 2)
		close(n--);
	}
	execvp(cmd, (void *) argv);
	perror(cmd);
	_exit(127);
    case -1:
	goto err;
    default:
	if (pidp) pidp->id = pid;
    }
#else
    const int is_console = lua_isboolean(L, -1) && lua_toboolean(L, -1);
    const int std_redirect = (in_fdp || out_fdp || err_fdp);
    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    char line[MAX_PATHNAME*2];  /* command line */
    int res;

    /* fill command line */
    {
	char *cp = line;
	int len = lua_rawlen(L, 1);

	/* filename */
	if (len >= sizeof(line) - 2)
	    return 0;
	memcpy(cp, cmd, len);
	cp += len;
	*cp++ = ' ';

	if (lua_istable(L, 2)) {
	    const char *arg, *endp = line + sizeof(line) - 2;
	    int i;

	    for (i = 1; i < MAX_ARGS; ++i) {
		lua_rawgeti(L, 2, i);
		arg = lua_tostring(L, -1);
		lua_pop(L, 1);
		if (!arg) break;
		*cp++ = '"';
		while (*arg && cp < endp) {
		    if (*arg == '"' || *arg == '\\')
			*cp++ = '\\';  /* escape special chars */
		    *cp++ = *arg++;
		}
		if (cp + 3 >= endp)
		    return 0;
		*cp++ = '"';
		*cp++ = ' ';
	    }
	}
	*(--cp) = '\0';
    }

    memset(&si, 0, sizeof(STARTUPINFO));
    si.cb = sizeof(STARTUPINFO);
    si.dwFlags = STARTF_USESHOWWINDOW;

    memset(&pi, 0, sizeof(PROCESS_INFORMATION));

    /* redirect std. handles */
    if (std_redirect) {
	const HANDLE hProc = GetCurrentProcess();

	res = 0;  /* go to err by default */
	si.dwFlags |= STARTF_USESTDHANDLES;
	si.wShowWindow = SW_MINIMIZE;
	si.hStdInput = si.hStdOutput = si.hStdError = INVALID_HANDLE_VALUE;

	if (!DuplicateHandle(hProc,
	 in_fdp ? *in_fdp : GetStdHandle(STD_INPUT_HANDLE),
	 hProc, &si.hStdInput, 0, TRUE, DUPLICATE_SAME_ACCESS) && in_fdp)
	    goto std_err;

	if (!DuplicateHandle(hProc,
	 out_fdp ? *out_fdp : GetStdHandle(STD_OUTPUT_HANDLE),
	 hProc, &si.hStdOutput, 0, TRUE, DUPLICATE_SAME_ACCESS) && out_fdp)
	    goto std_err;

	if (!DuplicateHandle(hProc,
	 err_fdp ? *err_fdp : GetStdHandle(STD_ERROR_HANDLE),
	 hProc, &si.hStdError, 0, TRUE, DUPLICATE_SAME_ACCESS) && err_fdp)
	    goto std_err;
    }
    res = CreateProcessA(NULL, line, NULL, NULL, TRUE,
     is_console ? CREATE_NEW_CONSOLE : 0, NULL, NULL, &si, &pi);
    if (std_redirect) {
 std_err:
	CloseHandle(si.hStdInput);
	CloseHandle(si.hStdOutput);
	CloseHandle(si.hStdError);
    }
    if (!res) goto err;
    CloseHandle(pi.hThread);
    if (!pidp)
	CloseHandle(pi.hProcess);
    else {
	pidp->h = pi.hProcess;
	pidp->id = pi.dwProcessId;
    }
#endif
    lua_pushboolean(L, 1);
    return 1;
 err:
    return sys_seterror(L, 0);
}
#undef MAX_ARGS

/*
 * Arguments: [success/failure (boolean) | status (number), close_vm (boolean)]
 */
static int
sys_exit (lua_State *L)
{
    const int status = !lua_isboolean(L, 1) ? lua_tointeger(L, 1)
     : (lua_toboolean(L, 1) ? EXIT_SUCCESS : EXIT_FAILURE);

    if (lua_toboolean(L, 2))
	lua_close(L);
    exit(status);
}

/*
 * Returns: system_time (microseconds), user_time (microseconds)
 */
static int
sys_times (lua_State *L)
{
    lua_Number st, ut;
#ifndef _WIN32
    struct rusage ru;

    if (!getrusage(RUSAGE_SELF, &ru)) {
	st = ru.ru_stime.tv_sec * 1000000L + ru.ru_stime.tv_usec;
	ut = ru.ru_utime.tv_sec * 1000000L + ru.ru_utime.tv_usec;
#else
    LARGE_INTEGER sl, ul;

    if (GetProcessTimes(GetCurrentProcess(), NULL, NULL,
     (FILETIME *) &sl, (FILETIME *) &ul)) {
	st = (lua_Number) sl.QuadPart / 10;
	ut = (lua_Number) ul.QuadPart / 10;
#endif
	lua_pushnumber(L, st);
	lua_pushnumber(L, ut);
	return 2;
    }
    return sys_seterror(L, 0);
}

/*
 * Returns: process_identifier (number)
 */
static int
sys_getpid (lua_State *L)
{
#ifndef _WIN32
    lua_pushnumber(L, getpid());
#else
    lua_pushnumber(L, GetCurrentProcessId());
#endif
    return 1;
}


/*
 * Arguments: [process_identifier (number)]
 * Returns: pid_udata
 */
static int
sys_pid (lua_State *L)
{
    const int id = luaL_optinteger(L, 1, -1);
    struct sys_pid *pidp = lua_newuserdata(L, sizeof(struct sys_pid));

    luaL_getmetatable(L, PID_TYPENAME);
    lua_setmetatable(L, -2);

    pidp->id = id;
#ifdef _WIN32
    if (id != -1) {
	if (id == (int) GetCurrentProcessId()) {
	    pidp->h = GetCurrentProcess();
	}
	else {
	    pidp->h = OpenProcess(PROCESS_ALL_ACCESS, FALSE, id);
	    if (pidp->h == NULL)
		return sys_seterror(L, 0);
	}
    }
#endif
    return 1;
}

/*
 * Arguments: pid_udata
 */
static int
proc_close (lua_State *L)
{
    struct sys_pid *pidp = checkudata(L, 1, PID_TYPENAME);

    if (pidp->id != -1) {
#ifdef _WIN32
	CloseHandle(pidp->h);
	pidp->h = NULL;
#endif
	pidp->id = -1;
    }
    return 0;
}

/*
 * Arguments: pid_udata
 * Returns: process_identifier (number)
 */
static int
proc_id (lua_State *L)
{
    struct sys_pid *pidp = checkudata(L, 1, PID_TYPENAME);

    if (pidp->id != -1) {
	lua_pushinteger(L, pidp->id);
	return 1;
    }
    return 0;
}

/*
 * Arguments: pid_udata, priority (string: "realtime", "high", "normal", "idle")
 * Returns: boolean
 */
static int
proc_priority (lua_State *L)
{
    struct sys_pid *pidp = checkudata(L, 1, PID_TYPENAME);
    const char *s = luaL_checkstring(L, 2);
    int prio = 0;

    switch (*s) {
#ifndef _WIN32
    case 'r': prio = -20; break;
    case 'h': prio = -10; break;
    case 'n': prio = 0; break;
    case 'i': prio = 19; break;
#else
    case 'r': prio = REALTIME_PRIORITY_CLASS; break;
    case 'h': prio = HIGH_PRIORITY_CLASS; break;
    case 'n': prio = NORMAL_PRIORITY_CLASS; break;
    case 'i': prio = IDLE_PRIORITY_CLASS; break;
#endif
    default: luaL_argerror(L, 1, "invalid option");
    }

#ifndef _WIN32
    if (!setpriority(PRIO_PROCESS, pidp->id, prio)) {
#else
    if (SetPriorityClass(pidp->h, prio)) {
#endif
        lua_pushboolean(L, 1);
	return 1;
    }
    return sys_seterror(L, 0);
}

/*
 * Arguments: pid_udata
 * Returns: status (number)
 */
static int
proc_wait (lua_State *L)
{
    struct sys_pid *pidp = checkudata(L, 1, PID_TYPENAME);
    int status;

    sys_vm_leave();
#ifndef _WIN32
    while ((waitpid(pidp->id, &status, 0)) == -1 && SYS_ERRNO == EINTR)
	continue;
    status = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#else
    WaitForSingleObject(pidp->h, INFINITE);
    GetExitCodeProcess(pidp->h, (DWORD *) &status);
#endif
    sys_vm_enter();

    lua_pushinteger(L, status);
    return 1;
}

/*
 * Arguments: pid_udata, [signal (string)]
 * Returns: [pid_udata]
 */
static int
proc_kill (lua_State *L)
{
    struct sys_pid *pidp = checkudata(L, 1, PID_TYPENAME);
#ifndef _WIN32
    const int signo = sig_flags[luaL_checkoption(L, 2, "TERM", sig_names)];
#endif

    if (pidp->id == -1) return 0;

#ifndef _WIN32
    if (!kill(pidp->id, signo)) {
#else
    if (TerminateProcess(pidp->h, (unsigned int) -1)) {
#endif
	lua_settop(L, 1);
	return 1;
    }
    return sys_seterror(L, 0);
}

/*
 * Arguments: pid_udata
 * Returns: string
 */
static int
proc_tostring (lua_State *L)
{
    struct sys_pid *pidp = checkudata(L, 1, PID_TYPENAME);

    if (pidp->id != -1)
	lua_pushfstring(L, PID_TYPENAME " (%d)", pidp->id);
    else
	lua_pushliteral(L, PID_TYPENAME " (closed)");
    return 1;
}


#define PROC_METHODS \
    {"run",		sys_run}, \
    {"spawn",		sys_spawn}, \
    {"exit",		sys_exit}, \
    {"times",		sys_times}, \
    {"getpid",		sys_getpid}, \
    {"pid",		sys_pid}

static luaL_reg pid_meth[] = {
    {"close",		proc_close},
    {"id",		proc_id}, \
    {"priority",	proc_priority},
    {"wait",		proc_wait},
    {"kill",		proc_kill},
    {"__tostring",	proc_tostring},
#ifdef _WIN32
    {"__gc",		proc_close},
#endif
    {NULL, NULL}
};
