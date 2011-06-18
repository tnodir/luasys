/* Lua System: Win32 specifics: Service */

#define WSVC_TYPENAME	"sys.win32.service"


/*
 * Arguments: name (string), filename (string), [manual_start (boolean)]
 * Returns: [boolean]
 */
static int
svc_install (lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    int pathlen = lua_rawlen(L, 2);
    const int manual = lua_toboolean(L, 3);
    SC_HANDLE mngr = OpenSCManager(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
    SC_HANDLE sc = NULL;

    if (!pathlen || ++pathlen >= MAX_PATHNAME)  /* include space & term. '\0' */
	return 0;
    if (mngr) {
	char path[MAX_PATHNAME*2];
	const int n = GetModuleFileNameA(NULL, path, MAX_PATHNAME);

	if (n) {
	    path[n] = ' ';
	    memcpy(path + n + 1, lua_tostring(L, 2), pathlen);
	    sc = CreateServiceA(mngr, name, name,
	     SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
	     manual ? SERVICE_DEMAND_START : SERVICE_AUTO_START,
	     SERVICE_ERROR_NORMAL,
	     path, 0, 0, 0, 0, 0);
	    if (sc) CloseServiceHandle(sc);
	}
	CloseServiceHandle(mngr);
    }
    if (sc) {
	lua_pushboolean(L, 1);
	return 1;
    }
    return sys_seterror(L, 0);
}

/*
 * Arguments: name (string)
 * Returns: [boolean]
 */
static int
svc_uninstall (lua_State *L)
{
    const char *name = lua_tostring(L, 1);
    SC_HANDLE mngr = OpenSCManager(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
    int res = 0;

    if (mngr) {
	SC_HANDLE sc = OpenServiceA(mngr, name, SERVICE_ALL_ACCESS | DELETE);

	if (sc) {
	    SERVICE_STATUS status;
	    int n = 2;  /* count of attempts to stop the service */

	    do {
		if (QueryServiceStatus(sc, &status)
		 && status.dwCurrentState == SERVICE_STOPPED) {
		    res = DeleteService(sc);
		    break;
		}
		ControlService(sc, SERVICE_CONTROL_STOP, &status);
		Sleep(1000);
	    } while (--n);
	    CloseServiceHandle(sc);
	}
	CloseServiceHandle(mngr);
    }
    if (res) {
	lua_pushboolean(L, 1);
	return 1;
    }
    return sys_seterror(L, 0);
}


/* Service global variables */
static struct {
    SERVICE_STATUS status;
    SERVICE_STATUS_HANDLE hstatus;
    HANDLE event;
    int accept_pause_cont;
} g_Service;

static void WINAPI
svc_controller (DWORD code)
{
    int accept = 0;

    switch (code) {
    case SERVICE_CONTROL_SHUTDOWN:
    case SERVICE_CONTROL_STOP:
	g_Service.status.dwCurrentState = SERVICE_STOP_PENDING;
	accept = 1;
	break;
    case SERVICE_CONTROL_PAUSE:
    case SERVICE_CONTROL_CONTINUE:
	if (g_Service.accept_pause_cont) {
	    g_Service.status.dwCurrentState = (code == SERVICE_CONTROL_PAUSE)
	     ? SERVICE_PAUSE_PENDING : SERVICE_CONTINUE_PENDING;
	    accept = 1;
	}
	break;
    }

    SetServiceStatus(g_Service.hstatus, &g_Service.status);
    if (accept)
	SetEvent(g_Service.event);
}

static void WINAPI
svc_main (DWORD argc, char *argv[])
{
    (void) argc;

    g_Service.hstatus = RegisterServiceCtrlHandlerA(argv[0], svc_controller);
    if (g_Service.hstatus) {
	/* initialise service status */
	g_Service.status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
	g_Service.status.dwControlsAccepted = SERVICE_ACCEPT_STOP
	 | SERVICE_ACCEPT_SHUTDOWN
	 | (g_Service.accept_pause_cont ? SERVICE_ACCEPT_PAUSE_CONTINUE : 0);
	g_Service.status.dwWin32ExitCode = NO_ERROR;
	g_Service.status.dwServiceSpecificExitCode = 0;
	g_Service.status.dwCheckPoint = 0;
	g_Service.status.dwWaitHint = 10000;

	g_Service.status.dwCurrentState = SERVICE_RUNNING;
	SetServiceStatus(g_Service.hstatus, &g_Service.status);
    }
    SetEvent(g_Service.event);
}

static DWORD WINAPI
svc_start (void *name)
{
    SERVICE_TABLE_ENTRY table[] = {
	{name, svc_main},
	{NULL, NULL}
    };
    return !StartServiceCtrlDispatcher(table);
}

/*
 * Arguments: name (string), [accept pause & continue (boolean)]
 * Returns: [svc_udata]
 */
static int
svc_handle (lua_State *L)
{
    char *svc_name = (char *) lua_tostring(L, 1);
    const int accept_pause_cont = lua_toboolean(L, 2);
    HANDLE hThr;
    DWORD id;

    if (g_Service.event)
	luaL_argerror(L, 0, "already running");
    else {
	HANDLE *hep = lua_newuserdata(L, sizeof(HANDLE));
	luaL_getmetatable(L, WSVC_TYPENAME);
	lua_setmetatable(L, -2);

	*hep = CreateEvent(NULL, TRUE, FALSE, NULL);  /* manual-reset */
	if (!*hep) goto err;

	g_Service.event = *hep;
	g_Service.accept_pause_cont = accept_pause_cont;
    }

    hThr = CreateThread(NULL, 4096, svc_start, svc_name, 0, &id);
    if (hThr != NULL) {
	CloseHandle(hThr);

	sys_vm_leave();
	WaitForSingleObject(g_Service.event, INFINITE);
	ResetEvent(g_Service.event);
	sys_vm_enter();

	if (g_Service.hstatus)
	    return 1;
    }
 err:
    return sys_seterror(L, 0);
}

/*
 * Arguments: svc_udata
 */
static int
svc_close (lua_State *L)
{
    HANDLE *hep = checkudata(L, 1, WSVC_TYPENAME);
    if (*hep) {
	CloseHandle(*hep);
	*hep = NULL;
    }
    return 0;
}

/*
 * Arguments: svc_udata, [status (string: "stopped", "paused", "running")]
 * Returns: [svc_udata | control_code (string: "stop", "pause", "continue")]
 */
static int
svc_status (lua_State *L)
{
    const char *s = lua_tostring(L, 2);

    if (s) {
	int st;
	switch (s[0]) {
	case 's': st = SERVICE_STOPPED; break;
	case 'p': st = SERVICE_PAUSED; break;
	case 'r': st = SERVICE_RUNNING; break;
	default: return 0;
	}
	g_Service.status.dwCurrentState = st;

	sys_vm_leave();
	SetServiceStatus(g_Service.hstatus, &g_Service.status);
	Sleep(0);
	sys_vm_enter();

	lua_settop(L, 1);
    }
    else {
	switch (g_Service.status.dwCurrentState) {
	case SERVICE_STOPPED:
	case SERVICE_STOP_PENDING:
	    s = "stop"; break;
	case SERVICE_RUNNING:
	    s = "running"; break;
	case SERVICE_CONTINUE_PENDING:
	    s = "continue"; break;
	case SERVICE_PAUSE_PENDING:
	case SERVICE_PAUSED:
	    s = "pause"; break;
	}
	lua_pushstring(L, s);
    }
    return 1;
}

/*
 * Arguments: svc_udata, [timeout (milliseconds)]
 * Returns: [signalled/timedout (boolean)]
 */
static int
svc_wait (lua_State *L)
{
    HANDLE he = (HANDLE) lua_unboxpointer(L, 1, WSVC_TYPENAME);
    int res = luaL_optinteger(L, 2, INFINITE);

    sys_vm_leave();
    res = WaitForSingleObject(he, res);
    ResetEvent(he);
    sys_vm_enter();

    if (res != WAIT_FAILED) {
	lua_pushboolean(L, res == WAIT_OBJECT_0);
	return 1;
    }
    return 0;
}

/*
 * Arguments: svc_udata
 * Returns: string
 */
static int
svc_tostring (lua_State *L)
{
    HANDLE he = (HANDLE) lua_unboxpointer(L, 1, WSVC_TYPENAME);
    lua_pushfstring(L, WSVC_TYPENAME " (%p)", he);
    return 1;
}


static luaL_reg svc_meth[] = {
    {"status",		svc_status},
    {"wait",		svc_wait},
    {"__tostring",	svc_tostring},
    {"__gc",		svc_close},
    {NULL, NULL}
};

static luaL_reg svc_lib[] = {
    {"install",		svc_install},
    {"uninstall",	svc_uninstall},
    {"handle",		svc_handle},
    {NULL, NULL}
};


static void
luaopen_sys_win32_service (lua_State *L)
{
    luaL_newmetatable(L, WSVC_TYPENAME);
    lua_pushvalue(L, -1);  /* push metatable */
    lua_setfield(L, -2, "__index");  /* metatable.__index = metatable */
    luaL_register(L, NULL, svc_meth);

    luaL_register(L, "sys.win32.service", svc_lib);
    lua_pop(L, 2);
}
