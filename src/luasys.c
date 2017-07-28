/* System library for Lua */

#include "common.h"

#include <sys/stat.h>


#ifndef _WIN32

#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/sysctl.h>

#define SYS_FILE_PERMISSIONS	0644  /* default file permissions */

#else

#include "win32/getloadavg.c"

#endif


/*
 * Arguments: ..., [number]
 * Returns: string
 */
static int
sys_strerror (lua_State *L)
{
  const int err = (int)luaL_optinteger(L, -1, SYS_ERRNO);

#ifndef _WIN32
#if defined(BSD) || (_POSIX_C_SOURCE >= 200112L || _XOPEN_SOURCE >= 600)
  char buf[512];

  if (!err) goto success;

  if (!strerror_r(err, buf, sizeof(buf))) {
    lua_pushstring(L, buf);
    return 1;
  }
#endif
  lua_pushstring(L, strerror(err));
#else
  const int flags = FORMAT_MESSAGE_IGNORE_INSERTS
   | FORMAT_MESSAGE_FROM_SYSTEM;
  WCHAR buf[512];

  if (!err) goto success;

  if (is_WinNT
   ? FormatMessageW(flags, NULL, err, 0, buf, sizeof(buf) / sizeof(buf[0]), NULL)
   : FormatMessageA(flags, NULL, err, 0, (char *) buf, sizeof(buf), NULL)) {
    char *s = filename_to_utf8(buf);
    if (s) {
      char *cp;
      for (cp = s; *cp != '\0'; ++cp) {
        if (*cp == '\r' || *cp == '\n')
          *cp = ' ';
      }

      lua_pushstring(L, s);
      free(s);
      return 1;
    }
  }
  lua_pushfstring(L, "System error %d", err);
#endif
  return 1;
 success:
  lua_pushliteral(L, "OK");
  return 1;
}

/*
 * Returns: nil, string
 */
int
sys_seterror (lua_State *L, int err)
{
  if (err) {
#ifndef _WIN32
    errno = err;
#else
    SetLastError(err);
#endif
  }
  lua_pushnil(L);
  sys_strerror(L);
  lua_pushvalue(L, -1);
  lua_setglobal(L, SYS_ERROR_MESSAGE);
  return 2;
}


/*
 * Returns: number_of_processors (number)
 */
static int
sys_nprocs (lua_State *L)
{
#ifndef _WIN32
  int n = 1;
#if defined(_SC_NPROCESSORS_ONLN)
  n = sysconf(_SC_NPROCESSORS_ONLN);
  if (n == -1) n = 1;
#elif defined(BSD)
  int mib[2];
  size_t len = sizeof(int);

  mib[0] = CTL_HW;
  mib[1] = HW_NCPU;
  sysctl(mib, 2, &n, &len, NULL, 0);
#endif
  lua_pushinteger(L, n);
#else
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  lua_pushinteger(L, si.dwNumberOfProcessors);
#endif
  return 1;
}

/*
 * Returns: processors_load_average (number), is_per_cpu (boolean)
 */
static int
sys_loadavg (lua_State *L)
{
  double loadavg;
#ifndef _WIN32
  const int res = 0;

  if (getloadavg(&loadavg, 1) == 1) {
    const int is_per_cpu = 1;
#else
  const int res = getloadavg(&loadavg);

  if (!res) {
    const int is_per_cpu = 0;
#endif
    lua_pushnumber(L, (lua_Number) loadavg);
    lua_pushboolean(L, is_per_cpu);
    return 2;
  }
  return sys_seterror(L, res);
}

/*
 * Arguments: [number_of_files (number)]
 * Returns: number_of_files (number)
 */
static int
sys_limit_nfiles (lua_State *L)
{
#ifndef _WIN32
  const int narg = lua_gettop(L);
  struct rlimit rlim;

  rlim.rlim_max = 0;
  getrlimit(RLIMIT_NOFILE, &rlim);
  lua_pushinteger(L, rlim.rlim_max);

  if (narg != 0) {
    const int n = lua_tointeger(L, 1);

    rlim.rlim_cur = rlim.rlim_max = n;
    if (setrlimit(RLIMIT_NOFILE, &rlim))
      return sys_seterror(L, 0);
  }
  return 1;
#else
  (void) L;

  lua_pushinteger(L, -1);
  return 1;
#endif
}

/*
 * Arguments: string
 * Returns: number
 */
static int
sys_toint (lua_State *L)
{
  const char *s = lua_tostring(L, 1);
  int num = 0, sign = 1;

  if (s) {
    switch (*s) {
    case '-': sign = -1;
    case '+': ++s;
    }
    while (*s >= '0' && *s <= '9')
      num = (num << 3) + (num << 1) + (*s++ & ~'0');
  }
  lua_pushinteger(L, sign * num);
  return 1;
}

/*
 * Arguments: error_handler (function), function, any ...
 * Returns: status (boolean), any ...
 */
static int
sys_xpcall (lua_State *L)
{
  const int status = lua_pcall(L, lua_gettop(L) - 2, LUA_MULTRET, 1);

  lua_pushboolean(L, !status);
  lua_insert(L, 2);
  return lua_gettop(L) - 1;
}


#include "isa/fcgi/sys_fcgi.c"
#include "mem/sys_mem.c"
#include "thread/sys_thread.c"

#ifndef _WIN32
#include "sys_unix.c"
#else
#include "win32/sys_win32.c"
#endif

#include "sys_file.c"
#include "sys_date.c"
#include "sys_env.c"
#include "sys_evq.c"
#include "sys_fs.c"
#include "sys_log.c"
#include "sys_proc.c"
#include "sys_rand.c"


static luaL_Reg sys_lib[] = {
  {"strerror",		sys_strerror},
  {"nprocs",		sys_nprocs},
  {"loadavg",		sys_loadavg},
  {"limit_nfiles",	sys_limit_nfiles},
  {"toint",		sys_toint},
  {"xpcall",		sys_xpcall},
  DATE_METHODS,
  ENV_METHODS,
  EVQ_METHODS,
  FCGI_METHODS,
  FD_METHODS,
  FS_METHODS,
  LOG_METHODS,
  PROC_METHODS,
  RAND_METHODS,
#ifndef _WIN32
  UNIX_METHODS,
#endif
  {NULL, NULL}
};


/*
 * Arguments: ..., sys_lib (table)
 */
static void
createmeta (lua_State *L)
{
  const int top = lua_gettop(L);
  const struct meta_s {
    const char *tname;
    luaL_Reg *meth;
    int is_index;
  } meta[] = {
    {DIR_TYPENAME,		dir_meth,	0},
    {EVQ_TYPENAME,		evq_meth,	1},
    {FD_TYPENAME,		fd_meth,	1},
    {PERIOD_TYPENAME,		period_meth,	1},
    {PID_TYPENAME,		pid_meth,	1},
    {LOG_TYPENAME,		log_meth,	0},
    {RAND_TYPENAME,		rand_meth,	0},
  };
  int i;

  for (i = 0; i < (int) (sizeof(meta) / sizeof(struct meta_s)); ++i) {
    luaL_newmetatable(L, meta[i].tname);
    if (meta[i].is_index) {
      lua_pushvalue(L, -1);  /* push metatable */
      lua_setfield(L, -2, "__index");  /* metatable.__index = metatable */
    }
    luaL_setfuncs(L, meta[i].meth, 0);
    lua_pop(L, 1);
  }

  /* Predefined file handles */
  luaL_getmetatable(L, FD_TYPENAME);
  {
    const char *std[] = {"stdin", "stdout", "stderr"};

    for (i = 3; i--; ) {
#ifndef _WIN32
      const fd_t fd = i;
#else
      const fd_t fd = GetStdHandle(i == 0 ? STD_INPUT_HANDLE
       : (i == 1 ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE));
#endif
      lua_pushstring(L, std[i]);
      lua_boxinteger(L, fd);
      lua_pushvalue(L, -3);  /* metatable */
      lua_pushboolean(L, 1);
      lua_rawseti(L, -2, (int) ((lua_Integer) fd));  /* don't close std. handles */
      lua_setmetatable(L, -2);
      lua_rawset(L, top);
    }
  }
  lua_settop(L, top);
}


LUALIB_API int
luaopen_sys (lua_State *L)
{
  luaL_register(L, LUA_SYSLIBNAME, sys_lib);
  createmeta(L);

  signal_init();

#ifdef _WIN32
  luaopen_sys_win32(L);
#endif
  luaopen_sys_mem(L);
  luaopen_sys_thread(L);

  return 1;
}
