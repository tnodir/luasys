#ifndef COMMON_H
#define COMMON_H

#define _FILE_OFFSET_BITS  64

#include <sys/types.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>


#ifdef _WIN32

#define _WIN32_WINNT	0x0600

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef WIN32_LEAN_AND_MEAN

#include <winsock2.h>
#include <mmsystem.h>	/* timeGetTime */

#ifndef ULONG_PTR
#define ULONG_PTR	DWORD
#endif

#if (_WIN32_WINNT >= 0x0500)
#define InitCriticalSection(cs)		InitializeCriticalSectionAndSpinCount(cs, 3000)
#else
#define InitCriticalSection(cs)		(InitializeCriticalSection(cs), TRUE)
#endif

#define SIGINT		CTRL_C_EVENT
#define SIGQUIT		CTRL_BREAK_EVENT
#define SIGHUP		CTRL_LOGOFF_EVENT
#define SIGTERM		CTRL_SHUTDOWN_EVENT
#define NSIG 		(SIGTERM + 1)

#define SYS_ERRNO	GetLastError()
#define SYS_EAGAIN(e)	((e) == WSAEWOULDBLOCK)

extern int is_WinNT;

#else

#include <sys/time.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>

#define SYS_ERRNO	errno
#define SYS_EAGAIN(e)	((e) == EAGAIN || (e) == EWOULDBLOCK)

#define SYS_SIGINTR	SIGWINCH

#endif


#define LUA_LIB

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "luasys.h"


#if LUA_VERSION_NUM < 502
#define lua_rawlen		lua_objlen
#define luaL_typeerror		luaL_typerror
#endif


#ifdef NO_CHECK_UDATA
#define checkudata(L,i,tname)	lua_touserdata(L, i)
#else
#define checkudata(L,i,tname)	luaL_checkudata(L, i, tname)
#endif

#define lua_boxpointer(L,u) \
    (*(void **) (lua_newuserdata(L, sizeof(void *))) = (u))
#define lua_unboxpointer(L,i,tname) \
    (*(void **) (checkudata(L, i, tname)))

#define lua_boxinteger(L,n) \
    (*(lua_Integer *) (lua_newuserdata(L, sizeof(lua_Integer))) = (lua_Integer) (n))
#define lua_unboxinteger(L,i,tname) \
    (*(lua_Integer *) (checkudata(L, i, tname)))


/*
 * 64-bit integers
 */

#if defined(_MSC_VER) || defined(__BORLANDC__)
typedef __int64	int64_t;
#endif

#define INT64_MAKE(lo,hi)	(((int64_t) (hi) << 32) | (unsigned int) (lo))
#define INT64_LOW(x)		((unsigned int) (x))
#define INT64_HIGH(x)		((unsigned int) ((x) >> 32))


/*
 * File and Socket Handles
 */

#define FD_TYPENAME	"sys.handle"

#ifdef _WIN32
typedef HANDLE	fd_t;
typedef SOCKET	sd_t;
#else
typedef int	fd_t;
typedef int	sd_t;
#endif


/*
 * Buffer Management
 */

#define MAX_PATHNAME	512

#define SYS_BUFIO_TAG	"bufio__"  /* key to indicate buffer I/O */
#define SYS_BUFSIZE	4096

struct membuf;

struct sys_buffer {
    union {
	const char *r;  /* read from buffer */
	char *w;  /* write to buffer */
    } ptr;
    size_t size;
    struct membuf *mb;
};

int sys_buffer_read_init (lua_State *L, int idx, struct sys_buffer *sb);
void sys_buffer_read_next (struct sys_buffer *sb, size_t n);

void sys_buffer_write_init (lua_State *L, int idx, struct sys_buffer *sb,
                            char *buf, size_t buflen);
int sys_buffer_write_next (lua_State *L, struct sys_buffer *sb,
                           char *buf, size_t buflen);
int sys_buffer_write_done (lua_State *L, struct sys_buffer *sb,
                           char *buf, size_t tail);


/*
 * Error Reporting
 */

#define SYS_ERROR_MESSAGE	"errorMessage"

int sys_seterror (lua_State *L, int err);


/*
 * Threading
 */

struct sys_thread;

void sys_set_thread (struct sys_thread *td);
struct sys_thread *sys_get_thread (void);

struct sys_thread *sys_get_vmthread (struct sys_thread *);
struct lua_State *sys_lua_tothread (struct sys_thread *);

void sys_vm2_enter (struct sys_thread *td);
void sys_vm2_leave (struct sys_thread *td);

void sys_vm_enter (void);
void sys_vm_leave (void);

struct sys_thread *sys_new_thread (struct sys_thread *td);
struct sys_thread *sys_del_thread (struct sys_thread *td);


/*
 * Object Events
 */

#define SYS_TRIGGER_TAG	"evq__"

typedef void *		sys_trigger_t;

typedef sys_trigger_t *(*sys_get_trigger_t) (lua_State *L, struct sys_thread **tdp);

enum {
    SYS_EVREAD	= 1,
    SYS_EVWRITE	= 2,
    SYS_EVEOF	= 4,
    SYS_EVDEL	= 8
};

int sys_trigger_notify (sys_trigger_t *trigger, int flags);


/*
 * Time
 */

typedef int	msec_t;

#ifdef _WIN32
#define get_milliseconds	timeGetTime
#else
msec_t get_milliseconds (void);
#endif

#define TIMEOUT_INFINITE	((msec_t) -1)


/*
 * Convert Windows OS filenames to UTF-8
 */

#ifdef _WIN32
void *utf8_to_filename (const char *s);
char *filename_to_utf8 (const void *s);
#endif


#endif
