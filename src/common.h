#ifndef COMMON_H
#define COMMON_H

#define _GNU_SOURCE	/* pthread_*affinity_np */

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

#if defined(_MSC_VER) || defined(__BORLANDC__)
typedef SSIZE_T		ssize_t;
#endif

#ifndef ULONG_PTR
typedef SIZE_T		ULONG_PTR, DWORD_PTR;
#endif

#if (_WIN32_WINNT >= 0x0500)
#define InitCriticalSection(cs)		InitializeCriticalSectionAndSpinCount(cs, 3000)
#else
#define InitCriticalSection(cs)		(InitializeCriticalSection(cs), TRUE)
#endif

#define SYS_ERRNO	GetLastError()
#define SYS_EAGAIN(e)	((e) == WSAEWOULDBLOCK)

#else

#include <sys/time.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>

#include <pthread.h>
#include <sched.h>

#define SYS_ERRNO	errno
#define SYS_EAGAIN(e)	((e) == EAGAIN || (e) == EWOULDBLOCK)

#define SYS_SIGINTR	SIGUSR2

#endif


#define LUA_LIB

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "luasys.h"


#if LUA_VERSION_NUM < 502
#define lua_rawlen		lua_objlen
#define lua_resume(L,from,n)	lua_resume((L), (n))
#define luaL_setfuncs(L,l,n)	luaL_register((L), NULL, (l))

#define luaL_newlibtable(L,l) \
	lua_createtable(L, 0, sizeof(l) / sizeof((l)[0]) - 1)
#define luaL_newlib(L,l)	(luaL_newlibtable((L), (l)), luaL_setfuncs((L), (l), 0))

#define lua_rawgetp(L,idx,p) \
	(lua_pushlightuserdata((L), (p)), \
	 lua_rawget((L), (idx) - ((idx) < 0 && (idx) > -99 ? 1 : 0)))
#define lua_rawsetp(L,idx,p) \
	(lua_pushlightuserdata((L), (p)), lua_insert((L), -2), \
	 lua_rawset((L), (idx) - ((idx) < 0 && (idx) > -99 ? 1 : 0)))

#define luai_writestringerror(s,p) \
	(fprintf(stderr, (s), (p)), fflush(stderr))

#define lua_pushunsigned(L,u)	lua_pushnumber((L), (lua_Number) (u))
#define lua_tounsigned(L,n)	(unsigned int) lua_tonumber((L), (n))
#define luaL_optunsigned(L,n,d)	(unsigned int) luaL_optnumber((L), (n), (lua_Number) (d))

#else
#define luaL_register(L,n,l)	luaL_newlib((L), (l))
#define lua_setfenv		lua_setuservalue
#define lua_getfenv		lua_getuservalue
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
typedef __int64			int64_t;
typedef unsigned __int64	uint64_t;
#else
#include <stdint.h>
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

#ifndef _WIN32

#if !defined(PTHREAD_MUTEX_RECURSIVE)
extern int pthread_mutexattr_setkind_np (pthread_mutexattr_t *attr, int kind);
#define pthread_mutexattr_settype	pthread_mutexattr_setkind_np
#define PTHREAD_MUTEX_RECURSIVE		PTHREAD_MUTEX_RECURSIVE_NP
#endif

#endif

struct sys_thread;

void sys_set_thread (struct sys_thread *td);
struct sys_thread *sys_get_thread (void);

struct sys_thread *sys_get_vmthread (struct sys_thread *);
struct lua_State *sys_lua_tothread (struct sys_thread *);

void sys_vm2_enter (struct sys_thread *td);
void sys_vm2_leave (struct sys_thread *td);

void sys_vm_enter (void);
void sys_vm_leave (void);

struct sys_thread *sys_new_thread (struct sys_thread *td, const int insert);
void sys_del_thread (struct sys_thread *td);

int sys_isintr (void);


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


#ifdef _WIN32

/*
 * Windows NT specifics
 */

#ifndef STATUS_CANCELLED
#define STATUS_CANCELLED	((DWORD) 0xC0000120L)
#endif

#ifndef FILE_SKIP_SET_EVENT_ON_HANDLE
typedef struct _OVERLAPPED_ENTRY {
    ULONG_PTR lpCompletionKey;
    LPOVERLAPPED lpOverlapped;
    ULONG_PTR Internal;
    DWORD dwNumberOfBytesTransferred;
} OVERLAPPED_ENTRY, *LPOVERLAPPED_ENTRY;
#endif

typedef BOOL (WINAPI *PCancelSynchronousIo) (HANDLE hThread);

typedef BOOL (WINAPI *PCancelIoEx) (HANDLE hThread, LPOVERLAPPED overlapped);

typedef BOOL (WINAPI *PGetQueuedCompletionStatusEx) (HANDLE handle,
	LPOVERLAPPED_ENTRY entries, ULONG count, PULONG n,
	DWORD timeout, BOOL alertable);

extern PCancelSynchronousIo pCancelSynchronousIo;
extern PCancelIoEx pCancelIoEx;
extern PGetQueuedCompletionStatusEx pGetQueuedCompletionStatusEx;

extern int is_WinNT;


/*
 * Convert Windows OS filenames to UTF-8
 */

void *utf8_to_filename (const char *s);
char *filename_to_utf8 (const void *s);

#endif


#endif
