
PLAT= none

LUA= /tmp/lua-5.1.4

T= sys.so
A= $(LUA)/src/libluasys.a

COPT= -O2 -fPIC -DNDEBUG
CWARNS = -W -Wall -Wextra -pedantic \
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
#	-Wunreachable-code

CFLAGS= $(COPT) $(CWARNS) $(MYCFLAGS) -I$(LUA)/src
LIBS= -lpthread $(MYLIBS)

MYCFLAGS=
MYLDFLAGS= -shared -Bdynamic
MYLIBS=

CC= gcc
MAKE_DLL= ld $(MYLDFLAGS) -o
AR= ar rcu
RANLIB= ranlib
RM= rm -f
STRIP= strip

PLATS= generic linux bsd osx

OBJS= luasys.o sock/sys_sock.o
LDOBJS= $(OBJS)


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
	$(MAKE) all MYCFLAGS="-DUSE_EPOLL" MYLIBS="-lrt"

bsd:
	$(MAKE) all MYCFLAGS="-DUSE_KQUEUE" LDOBJS="*.o"

osx:
	$(MAKE) all MYCFLAGS="-DUSE_KQUEUE" STRIP="echo strip" \
	MYLDFLAGS="-bundle -undefined dynamic_lookup /usr/lib/bundle1.o"

clean:
	$(RM) $(T) *.o */*.o


.PHONY: all $(PLATS) default a clean


luasys.o: luasys.c sys_comm.c sys_date.c sys_env.c sys_evq.c sys_file.c \
    sys_fs.c sys_log.c sys_proc.c sys_rand.c sys_unix.c common.h \
    thread/sys_thread.c thread/thread_dpool.c \
    thread/thread_msg.c thread/thread_sync.c \
    mem/sys_mem.c mem/membuf.c \
    event/evq.c event/epoll.c event/kqueue.c event/poll.c \
    event/select.c event/signal.c event/timeout.c \
    event/evq.h event/epoll.h event/kqueue.h event/poll.h \
    event/select.h event/timeout.h
sock/sys_sock.o: sock/sys_sock.c common.h
