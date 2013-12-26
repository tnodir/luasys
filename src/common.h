#ifndef COMMON_H
#define COMMON_H

#ifdef _WIN32

#define _WIN32_WINNT	0x0600

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <winsock2.h>
#include <mmsystem.h>	/* timeGetTime */

#else

#define _GNU_SOURCE	/* pthread_*affinity_np */

#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS	64
#define _LARGEFILE_SOURCE	1
#endif

#include <sys/time.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>

#include <pthread.h>
#include <sched.h>

#if defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#include <sys/param.h>
#endif

#endif


#include <sys/types.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>


#ifdef _WIN32

#if defined(_MSC_VER) || defined(__BORLANDC__)
typedef SSIZE_T		ssize_t;
#endif

#ifndef ULONG_PTR
typedef SIZE_T		ULONG_PTR, DWORD_PTR;
#endif

#if (_WIN32_WINNT >= 0x0500)
#define InitCriticalSection(cs) \
    InitializeCriticalSectionAndSpinCount(cs, 3000)
#else
#define InitCriticalSection(cs) \
    (InitializeCriticalSection(cs), TRUE)
#endif

#define SYS_ERRNO		GetLastError()
#define SYS_IS_EAGAIN(e)	((e) == WSAEWOULDBLOCK)

#else

#define SYS_ERRNO		errno
#define SYS_IS_EAGAIN(e)	((e) == EAGAIN || (e) == EWOULDBLOCK)

#define SYS_SIGINTR		SIGUSR2

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
#define luaL_newlib(L,l) \
    (luaL_newlibtable((L), (l)), luaL_setfuncs((L), (l), 0))

#define lua_rawgetp(L,idx,p) \
    (lua_pushlightuserdata((L), (p)), \
     lua_rawget((L), (idx) - ((idx) < 0 && (idx) > -99 ? 1 : 0)))
#define lua_rawsetp(L,idx,p) \
    (lua_pushlightuserdata((L), (p)), lua_insert((L), -2), \
     lua_rawset((L), (idx) - ((idx) < 0 && (idx) > -99 ? 1 : 0)))

#define luai_writestringerror(s,p) \
    (fprintf(stderr, (s), (p)), fflush(stderr))

#define lua_absindex(L,idx) \
    ((idx) < 0 && (idx) > -99 ? lua_gettop(L) + (idx) + 1 : (idx))

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
    (*(lua_Integer *) (lua_newuserdata(L, sizeof(lua_Integer))) \
     = (lua_Integer) (n))
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
 * Error Reporting
 */

#define SYS_ERROR_MESSAGE	"SYS_ERR"

int sys_seterror (lua_State *L, int err);


/*
 * Time
 */

typedef int	msec_t;

#ifdef _WIN32
#define sys_milliseconds	timeGetTime
#else
msec_t sys_milliseconds (void);
#endif

#define TIMEOUT_INFINITE	((msec_t) -1)


/*
 * Buffer Management
 */

#define MAX_PATHNAME	512

#define SYS_BUFIO_TAG	"bufio__"  /* key to indicate buffer I/O */
#define SYS_BUFSIZE	4096

struct membuf;

union sys_rwptr {
  const char *r;  /* read from buffer */
  char *w;  /* write to buffer */
};

struct sys_buffer {
  union sys_rwptr ptr;
  size_t size;
  struct membuf *mb;
};

int sys_buffer_read_init (lua_State *L, int idx, struct sys_buffer *sb);
void sys_buffer_read_next (struct sys_buffer *sb, const size_t n);

void sys_buffer_write_init (lua_State *L, int idx, struct sys_buffer *sb,
                            char *buf, const size_t buflen);
int sys_buffer_write_next (lua_State *L, struct sys_buffer *sb,
                           char *buf, const size_t buflen);
int sys_buffer_write_done (lua_State *L, struct sys_buffer *sb,
                           char *buf, const size_t tail);


/*
 * Threading
 */

struct sys_thread;

void sys_thread_set (struct sys_thread *td);
struct sys_thread *sys_thread_get (void);

struct lua_State *sys_thread_tolua (struct sys_thread *td);

void sys_thread_switch (struct sys_thread *td);

void sys_thread_check (struct sys_thread *td, lua_State *L);

void sys_vm2_enter (struct sys_thread *td);
void sys_vm2_leave (struct sys_thread *td);

void sys_vm_enter (lua_State *L);
void sys_vm_leave (lua_State *L);

struct sys_thread *sys_thread_new (lua_State *L,
                                   struct sys_thread *vmtd,
                                   struct sys_thread *vmtd2,
                                   const int push_udata);
void sys_thread_del (struct sys_thread *td);

int sys_thread_suspend (struct sys_thread *td, const msec_t timeout);
void sys_thread_resume (struct sys_thread *td);

int sys_eintr (void);


/*
 * Event Queue & Scheduler
 */

#define EVQ_TYPENAME	"sys.event_queue"

enum {
  SYS_ERR_SYSTEM = -1,
  SYS_ERR_TIMEOUT = 1,
  SYS_ERR_THROW
};

enum {
  EVQ_SCHED_OBJ = 0,
  EVQ_SCHED_TIMER,
  EVQ_SCHED_PID,
  EVQ_SCHED_DIRWATCH,
#ifdef _WIN32
  EVQ_SCHED_REGWATCH,
#endif
  EVQ_SCHED_SIGNAL,
  EVQ_SCHED_SOCKET
};

struct event_queue;

int sys_evq_loop (lua_State *L, struct event_queue *evq,
                  const msec_t timeout, const int linger,
                  const int once, const int evq_idx);

int sys_evq_sched_add (lua_State *L, const int evq_idx, const int type);
void sys_evq_sched_del (lua_State *L, void *ev_op, const int ev_added);

void sys_sched_event_added (lua_State *co, void *ev);
void sys_sched_event_ready (lua_State *co, void *ev);


#ifdef _WIN32

/*
 * Windows NT specifics
 */

#ifndef STATUS_CANCELLED
#define STATUS_CANCELLED	((DWORD) 0xC0000120L)
#endif

typedef BOOL (WINAPI *PCancelSynchronousIo) (HANDLE hThread);

typedef BOOL (WINAPI *PCancelIoEx) (HANDLE hThread, LPOVERLAPPED overlapped);

extern PCancelSynchronousIo pCancelSynchronousIo;
extern PCancelIoEx pCancelIoEx;

extern CRITICAL_SECTION g_CritSect;

#if defined(_WIN32_WCE) || defined(WIN32_VISTA)
#define is_WinNT	1
#else
extern int is_WinNT;
#endif


/* Windows Registry */

#define WREG_TYPENAME	"sys.win32.registry"


/*
 * Convert Windows OS filenames to UTF-8
 */

void *utf8_to_filename (const char *s);
char *filename_to_utf8 (const void *s);

#endif


#endif
