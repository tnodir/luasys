
PLAT= none

LUA= /opt/lua-5.2.3

T= sys.so
A= libluasys.a

COPT= -O2 -fPIC -DNDEBUG
CWARNS= -W -Wall -Wextra -pedantic \
	-Waggregate-return \
	-Wbad-function-cast \
	-Wcast-align \
	-Wcast-qual \
	-Wdeclaration-after-statement \
	-Wdisabled-optimization \
	-Wmissing-prototypes \
	-Wnested-externs \
	-Wpointer-arith \
	-Wshadow \
	-Wsign-compare \
	-Wstrict-prototypes \
	-Wundef \
	-Wwrite-strings \
	-Wpadded \
#	-Wunreachable-code

CFLAGS= $(COPT) $(CWARNS) $(MYCFLAGS) -I$(LUA)/src
LIBS= -lpthread $(MYLIBS)

MYCFLAGS=
MYLDFLAGS= -shared -Bdynamic
MYLIBS=

CC= gcc
MAKE_DLL= $(CC) $(MYLDFLAGS) -o
AR= ar rcu
RANLIB= ranlib
RM= rm -f
STRIP= strip

PLATS= generic linux bsd osx mingw

OBJS= luasys.o sock/sys_sock.o
LDOBJS= $(OBJS)


ifneq (,$(findstring Windows,$(OS)))
  UNAME_S= $(shell uname -s)
  ifneq (,$(findstring MINGW,$(UNAME_S)))
    T= sys.dll
    A= sys.lib

    MYCFLAGS= -DLUA_BUILD_AS_DLL
    MYLDFLAGS= -s -shared
    LIBS= $(LUA)/src/lua51.dll -lkernel32 -luser32 -lwinmm -lshell32 -ladvapi32 -lws2_32 -lpsapi
    OBJS+= isa/isapi/isapi_dll.o
  endif
endif


default: $(PLAT)

all:	$(T)

a:	$(A)

$(T): $(OBJS)
	$(MAKE_DLL) $@ $(LDOBJS) $(LIBS)
	$(STRIP) $@

$(A): $(OBJS)
	$(AR) $@ $?
	$(RANLIB) $@


none:
	@echo "Please choose a platform:"
	@echo "	$(PLATS)"

generic:
	$(MAKE) all MYCFLAGS=

linux:
	$(MAKE) all MYCFLAGS="-DUSE_EPOLL -DUSE_EVENTFD" MYLIBS="-lrt"

bsd:
	$(MAKE) all MYCFLAGS="-DUSE_KQUEUE" LDOBJS="*.o"

osx:
	$(MAKE) all MYCFLAGS="-DUSE_KQUEUE" STRIP="echo strip" \
	MYLDFLAGS="-bundle -undefined dynamic_lookup /usr/lib/bundle1.o"

mingw:	all

clean:
	$(RM) $(T) $(A) $(LDOBJS)


.PHONY: all $(PLATS) default a clean


luasys.o: luasys.c sys_comm.c sys_date.c sys_env.c sys_evq.c sys_file.c \
 sys_fs.c sys_log.c sys_proc.c sys_rand.c sys_unix.c common.h \
 thread/sys_thread.c thread/thread_dpool.c \
 thread/thread_pipe.c thread/thread_sync.c \
 mem/sys_mem.c mem/membuf.c \
 event/evq.c event/epoll.c event/kqueue.c event/poll.c \
 event/select.c event/signal.c event/timeout.c \
 event/evq.h event/epoll.h event/kqueue.h event/poll.h \
 event/select.h event/timeout.h \
 win32/sys_win32.c win32/win32_reg.c win32/win32_svc.c win32/win32_utf8.c
sock/sys_sock.o: sock/sys_sock.c sock/sock_addr.c common.h
isa/isapi/isapi_dll.o: isa/isapi/isapi_dll.c isa/isapi/isapi_ecb.c common.h
