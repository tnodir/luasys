/* Lua System: Networking */

#include "../common.h"

#ifdef _WIN32

#include <ws2tcpip.h>	/* Multicast */

#define IS_OVERLAPPED	(is_WinNT ? WSA_FLAG_OVERLAPPED : 0)

#define SHUT_WR		SD_SEND

typedef long		ssize_t;
typedef int		socklen_t;

#undef EINTR
#define EINTR		WSAEINTR
#define EINPROGRESS	WSAEINPROGRESS
#define EALREADY	WSAEALREADY

#else

#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>	/* TCP_NODELAY */
#include <netdb.h>

#if defined(__linux__)
#include <sys/sendfile.h>
#else
#include <sys/uio.h>		/* sendfile */
#endif

#ifdef __sun
#include <sys/filio.h>		/* FIONBIO */
#else
#include <sys/ioctl.h>		/* FIONBIO */
#endif

#define ioctlsocket	ioctl

#endif /* !WIN32 */


#define SD_TYPENAME	"sys.sock.handle"

#include "sock_addr.c"


/*
 * Returns: sd_udata
 */
static int
sock_new (lua_State *L)
{
    lua_boxinteger(L, -1);
    luaL_getmetatable(L, SD_TYPENAME);
    lua_setmetatable(L, -2);
    return 1;
}


#ifdef _WIN32

static int
sock_pair (int type, sd_t sv[2])
{
    struct sockaddr_in sa;
    sd_t sd;
    int res = -1, len = sizeof(struct sockaddr_in);

    sa.sin_family = AF_INET;
    sa.sin_port = 0;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if ((sd = WSASocket(AF_INET, type, 0, NULL, 0, IS_OVERLAPPED)) != -1) {
	if (!bind(sd, (struct sockaddr *) &sa, len)
	 && !listen(sd, 1)
	 && !getsockname(sd, (struct sockaddr *) &sa, &len)
	 && (sv[0] = WSASocket(AF_INET, type, 0, NULL, 0, IS_OVERLAPPED)) != -1) {
	    struct sockaddr_in sa2;
	    int len2;

	    sv[1] = (sd_t) -1;
	    if (!connect(sv[0], (struct sockaddr *) &sa, len)
	     && (sv[1] = accept(sd, (struct sockaddr *) &sa, &len)) != -1
	     && !getpeername(sv[0], (struct sockaddr *) &sa, &len)
	     && !getsockname(sv[1], (struct sockaddr *) &sa2, &len2)
	     && len == len2
	     && sa.sin_addr.s_addr == sa2.sin_addr.s_addr
	     && sa.sin_port == sa2.sin_port)
		res = 0;
	    else {
		closesocket(sv[0]);
		if (sv[1] != (sd_t) -1) closesocket(sv[1]);
	    }
	}
	closesocket(sd);
    }
    return res;
}

#endif


/*
 * Arguments: sd_udata, [type ("stream", "dgram"), domain ("inet", "inet6", "unix"),
 *	sd_udata (socketpair)]
 * Returns: [sd_udata]
 */
static int
sock_socket (lua_State *L)
{
    sd_t *sdp = checkudata(L, 1, SD_TYPENAME);
    const char *typep = lua_tostring(L, 2);
    const char *domainp = lua_tostring(L, 3);
    int type = SOCK_STREAM, domain = AF_INET;
    sd_t sd, sv[2];
    sd_t *pair_sdp = (lua_gettop(L) > 1 && lua_isuserdata(L, -1))
     ? checkudata(L, -1, SD_TYPENAME) : NULL;

    if (typep && typep[0] == 'd')
	type = SOCK_DGRAM;
    if (domainp) {
	if (domainp[0] == 'u')
	    domain = AF_UNIX;
	else if (domainp[0] == 'i' && domainp[1] == 'n' && domainp[2] == 'e'
	 && domainp[3] == 't' && domainp[4] == '6')
	    domain = AF_INET6;
    }

#ifndef _WIN32
    sd = (pair_sdp) ? socketpair(AF_UNIX, type, 0, sv)
     : socket(domain, type, 0);
#else
    sd = (pair_sdp) ? sock_pair(type, sv)
     : WSASocket(domain, type, 0, NULL, 0, IS_OVERLAPPED);
#endif

    if (sd != -1) {
	if (pair_sdp) {
	    *sdp = sv[0];
	    *pair_sdp = sv[1];
	} else
	    *sdp = sd;
	lua_settop(L, 1);
	return 1;
    }
    return sys_seterror(L, 0);
}

/*
 * Arguments: sd_udata
 * Returns: [boolean, sd_udata]
 */
static int
sock_close (lua_State *L)
{
    sd_t *sdp = checkudata(L, 1, SD_TYPENAME);

    if (*sdp != (sd_t) -1) {
#ifndef _WIN32
	int res;

	do res = close(*sdp);
	while (res == -1 && SYS_ERRNO == EINTR);
	lua_pushboolean(L, !res);
#else
	lua_pushboolean(L, !closesocket(*sdp));
#endif
	*sdp = (sd_t) -1;
	return 1;
    }
    return 0;
}

/*
 * Arguments: sd_udata
 * Returns: [sd_udata]
 */
static int
sock_shutdown (lua_State *L)
{
    sd_t sd = (sd_t) lua_unboxinteger(L, 1, SD_TYPENAME);

    /* SHUT_RD (SD_RECEIVE) has different behavior in unix and win32 */
    if (!shutdown(sd, SHUT_WR)) {
	lua_settop(L, 1);
	return 1;
    }
    return sys_seterror(L, 0);
}

/*
 * Arguments: sd_udata, nonblocking (boolean)
 * Returns: [sd_udata]
 */
static int
sock_nonblocking (lua_State *L)
{
    sd_t sd = (sd_t) lua_unboxinteger(L, 1, SD_TYPENAME);
    unsigned long opt = lua_toboolean(L, 2);

    lua_settop(L, 1);
    return !ioctlsocket(sd, FIONBIO, &opt) ? 1
     : sys_seterror(L, 0);
}

/*
 * Arguments: sd_udata, option (string),
 *	[value_lo (number), value_hi (number)]
 * Returns: [sd_udata | value_lo (number), value_hi (number)]
 */
static int
sock_sockopt (lua_State *L)
{
    static const int opt_flags[] = {
	SO_REUSEADDR, SO_TYPE, SO_ERROR, SO_DONTROUTE,
	SO_SNDBUF, SO_RCVBUF, SO_SNDLOWAT, SO_RCVLOWAT,
	SO_BROADCAST, SO_KEEPALIVE, SO_OOBINLINE, SO_LINGER,
#define OPTNAMES_TCP	12
	TCP_NODELAY,
#define OPTNAMES_IP	13
	IP_MULTICAST_TTL, IP_MULTICAST_IF, IP_MULTICAST_LOOP
    };
    static const char *const opt_names[] = {
	"reuseaddr", "type", "error", "dontroute",
	"sndbuf", "rcvbuf", "sndlowat", "rcvlowat",
	"broadcast", "keepalive", "oobinline", "linger",
	"tcp_nodelay",
	"multicast_ttl", "multicast_if", "multicast_loop", NULL
    };
#undef OPT_START
#define OPT_START	2
    sd_t sd = (sd_t) lua_unboxinteger(L, 1, SD_TYPENAME);
    const int optname = luaL_checkoption(L, OPT_START, NULL, opt_names);
    const int level = (optname < OPTNAMES_TCP) ? SOL_SOCKET
     : (optname < OPTNAMES_IP ? IPPROTO_TCP : IPPROTO_IP);
    const int optflag = opt_flags[optname];
    int optval[4];
    socklen_t optlen = sizeof(int);
    const int nargs = lua_gettop(L);

    if (nargs > OPT_START) {
	optval[0] = lua_tointeger(L, OPT_START + 1);
	if (nargs > OPT_START + 1) {
	    optval[1] = lua_tointeger(L, OPT_START + 2);
	    optlen *= 2;
	}
	if (!setsockopt(sd, level, optflag, (char *) &optval, optlen)) {
	    lua_settop(L, 1);
	    return 1;
	}
    }
    else if (!getsockopt(sd, level, optflag, (char *) &optval, &optlen)) {
	lua_pushinteger(L, optval[0]);
	if (optlen <= sizeof(int))
	    return 1;
	lua_pushinteger(L, optval[1]);
	return 2;
    }
    return sys_seterror(L, 0);
}

/*
 * Arguments: sd_udata, binary_address (multiaddr),
 *	[binary_address_ipv4 (interface) | interface_ipv6 (number),
 *	add/drop (boolean)]
 * Returns: [sd_udata]
 */
static int
sock_membership (lua_State *L)
{
    sd_t sd = (sd_t) lua_unboxinteger(L, 1, SD_TYPENAME);
    int len, af;
    const char *maddrp = sock_checkladdr(L, 2, &len, &af);
    const int optflag = !lua_isboolean(L, -1) || lua_toboolean(L, -1)
     ? IP_ADD_MEMBERSHIP : IP_DROP_MEMBERSHIP;
    union {
	struct ip_mreq ip;
#ifdef IPPROTO_IPV6
	struct ipv6_mreq ip6;
#endif
    } mr;
    int level, mr_len;

    memset(&mr, 0, sizeof(mr));
    if (af == AF_INET) {
	const char *ifacep = (lua_type(L, 3) == LUA_TSTRING)
	 ? sock_checkladdr(L, 3, &len, &af) : NULL;

	if (ifacep && af != AF_INET)
	    luaL_argerror(L, 3, "invalid interface");

	memcpy(&mr.ip.imr_multiaddr, maddrp, len);
	if (ifacep)
	    memcpy(&mr.ip.imr_interface, ifacep, len);

	level = IPPROTO_IP;
	mr_len = sizeof(struct ip_mreq);
    }
    else {
#ifdef IPPROTO_IPV6
	memcpy(&mr.ip6.ipv6mr_multiaddr, maddrp, len);
	mr.ip6.ipv6mr_interface = lua_tointeger(L, 3);

	level = IPPROTO_IPV6;
	mr_len = sizeof(struct ipv6_mreq);
#else
	luaL_argerror(L, 2, "invalid family");
#endif
    }

    if (!setsockopt(sd, level, optflag, (char *) &mr, mr_len)) {
	lua_settop(L, 1);
	return 1;
    }
    return sys_seterror(L, 0);
}

/*
 * Arguments: sd_udata, sock_addr_udata
 * Returns: [sd_udata]
 */
static int
sock_bind (lua_State *L)
{
    sd_t sd = (sd_t) lua_unboxinteger(L, 1, SD_TYPENAME);
    struct sock_addr *sap = checkudata(L, 2, SA_TYPENAME);

    if (!bind(sd, &sap->u.addr, sap->addrlen)) {
	lua_settop(L, 1);
	return 1;
    }
    return sys_seterror(L, 0);
}

/*
 * Arguments: sd_udata, [backlog (number)]
 * Returns: [sd_udata]
 */
static int
sock_listen (lua_State *L)
{
    sd_t sd = (sd_t) lua_unboxinteger(L, 1, SD_TYPENAME);
    const int backlog = luaL_optinteger(L, 2, SOMAXCONN);

    if (!listen(sd, backlog)) {
	lua_settop(L, 1);
	return 1;
    }
    return sys_seterror(L, 0);
}

/*
 * Arguments: sd_udata, new_sd_udata, [sock_addr_udata]
 * Returns: [new_sd_udata | false (EAGAIN)]
 */
static int
sock_accept (lua_State *L)
{
    sd_t sd = (sd_t) lua_unboxinteger(L, 1, SD_TYPENAME);
    sd_t *sdp = checkudata(L, 2, SD_TYPENAME);
    struct sock_addr *from = lua_isnoneornil(L, 3) ? NULL
     : checkudata(L, 3, SA_TYPENAME);
    struct sockaddr *sap = NULL;
    socklen_t *slp = NULL;

    if (from) {
	sap = &from->u.addr;
	slp = &from->addrlen;
    }
#ifndef _WIN32
    do sd = accept(sd, sap, slp);
    while (sd == -1 && SYS_ERRNO == EINTR);
#else
    sd = accept(sd, sap, slp);
#endif
    if (sd != (sd_t) -1) {
	*sdp = sd;
	lua_settop(L, 2);
	return 1;
    }
    else if (SYS_EAGAIN(SYS_ERRNO)) {
	lua_pushboolean(L, 0);
	return 1;
    }
    return sys_seterror(L, 0);
}

/*
 * Arguments: sd_udata, sock_addr_udata
 * Returns: [sd_udata | false (EINPROGRESS)]
 */
static int
sock_connect (lua_State *L)
{
    sd_t sd = (sd_t) lua_unboxinteger(L, 1, SD_TYPENAME);
    struct sock_addr *sap = checkudata(L, 2, SA_TYPENAME);
    int res;

    sys_vm_leave();
    do res = connect(sd, &sap->u.addr, sap->addrlen);
    while (res == -1 && SYS_ERRNO == EINTR);
    sys_vm_enter();

    if (!res || SYS_ERRNO == EINPROGRESS || SYS_ERRNO == EALREADY
#if defined(__FreeBSD__)
     || SYS_ERRNO == EADDRINUSE
#endif
     || SYS_EAGAIN(SYS_ERRNO)) {
	if (res) lua_pushboolean(L, 0);
	else lua_settop(L, 1);
	return 1;
    }
    return sys_seterror(L, 0);
}

/*
 * Arguments: sd_udata, {string | membuf_udata},
 *	[to (sock_addr_udata), options (string) ...]
 * Returns: [success/partial (boolean), count (number)]
 */
static int
sock_send (lua_State *L)
{
    static const int o_flags[] = {
	MSG_OOB, MSG_DONTROUTE,
    };
    static const char *const o_names[] = {
	"oob", "dontroute", NULL
    };
    sd_t sd = (sd_t) lua_unboxinteger(L, 1, SD_TYPENAME);
    const struct sock_addr *to = !lua_isuserdata(L, 3) ? NULL
     : checkudata(L, 3, SA_TYPENAME);
    struct sys_buffer sb;
    int nw;  /* number of chars actually send */
    unsigned int i, flags = 0;

    if (!sys_buffer_read_init(L, 2, &sb))
	luaL_argerror(L, 2, "buffer expected");
    for (i = lua_gettop(L); i > 3; --i) {
	flags |= o_flags[luaL_checkoption(L, i, NULL, o_names)];
    }
    sys_vm_leave();
    do nw = !to ? send(sd, sb.ptr.r, sb.size, flags)
     : sendto(sd, sb.ptr.r, sb.size, flags, &to->u.addr, to->addrlen);
    while (nw == -1 && SYS_ERRNO == EINTR);
    sys_vm_enter();
    if (nw == -1) {
	if (!SYS_EAGAIN(SYS_ERRNO))
	    return sys_seterror(L, 0);
	nw = 0;
    } else {
	sys_buffer_read_next(&sb, nw);
    }
    lua_pushboolean(L, ((size_t) nw == sb.size));
    lua_pushinteger(L, nw);
    return 2;
}

/*
 * Arguments: sd_udata, [count (number) | membuf_udata,
 *	from (sock_addr_udata), options (string) ...]
 * Returns: [string | count (number) | false (EAGAIN)]
 */
static int
sock_recv (lua_State *L)
{
    static const int o_flags[] = {
	MSG_OOB, MSG_PEEK,
#ifndef _WIN32
	MSG_WAITALL
#endif
    };
    static const char *const o_names[] = {
	"oob", "peek",
#ifndef _WIN32
	"waitall",
#endif
	NULL
    };
    sd_t sd = (sd_t) lua_unboxinteger(L, 1, SD_TYPENAME);
    size_t n = !lua_isnumber(L, 2) ? ~((size_t) 0)
     : (size_t) lua_tointeger(L, 2);
    struct sock_addr *from = !lua_isuserdata(L, 3) ? NULL
     : checkudata(L, 3, SA_TYPENAME);
    struct sockaddr *sap = NULL;
    socklen_t *slp = NULL;
    const size_t len = n;  /* how much total to read */
    size_t rlen;  /* how much to read */
    int nr;  /* number of bytes actually read */
    struct sys_buffer sb;
    char buf[SYS_BUFSIZE];
    unsigned int i, flags = 0;

    sys_buffer_write_init(L, 2, &sb, buf, sizeof(buf));

    for (i = lua_gettop(L); i > 3; --i) {
	flags |= o_flags[luaL_checkoption(L, i, NULL, o_names)];
    }
    if (from) {
	sap = &from->u.addr;
	slp = &from->addrlen;
    }
    do {
	rlen = (n <= sb.size) ? n : sb.size;
	sys_vm_leave();
#ifndef _WIN32
	do nr = recvfrom(sd, sb.ptr.w, rlen, flags, sap, slp);
	while (nr == -1 && SYS_ERRNO == EINTR);
#else
	nr = recvfrom(sd, sb.ptr.w, rlen, flags, sap, slp);
#endif
	sys_vm_enter();
	if (nr == -1) break;
	n -= nr;  /* still have to read `n' bytes */
    } while ((n != 0L && nr == (int) rlen)  /* until end of count or eof */
     && sys_buffer_write_next(L, &sb, buf, 0));
    if (nr <= 0 && len == n) {
	if (!nr || !SYS_EAGAIN(SYS_ERRNO)) goto err;
	lua_pushboolean(L, 0);
    } else {
	if (!sys_buffer_write_done(L, &sb, buf, nr))
	    lua_pushinteger(L, len - n);
    }
    return 1;
 err:
    return sys_seterror(L, 0);
}


#ifdef _WIN32

#define SYS_GRAN_MASK	(64 * 1024 - 1)

static DWORD
TransmitFileMap (SOCKET sd, HANDLE fd, DWORD n)
{
    HANDLE hmap = CreateFileMapping(fd, NULL, PAGE_READONLY, 0, 0, NULL);
    DWORD res = 0;

    if (hmap) {
	DWORD size_hi, size_lo;
	char *base = NULL;

	size_lo = GetFileSize(fd, &size_hi);
	if (size_lo != -1L || SYS_ERRNO == NO_ERROR) {
	    LONG off_hi = 0L, off_lo;
	    int64_t size;
	    DWORD len;

	    off_lo = SetFilePointer(fd, 0, &off_hi, SEEK_CUR);
	    size = INT64_MAKE(size_lo, size_hi) - INT64_MAKE(off_lo, off_hi);
	    len = (size < (int64_t) ~((DWORD) 0))
	     ? (DWORD) size : ~((DWORD) 0);

	    if (n <= 0 || n > len) n = len;
	    size_lo = (off_lo & SYS_GRAN_MASK);
	    base = MapViewOfFile(hmap, FILE_MAP_READ,
	     off_hi, (off_lo & ~SYS_GRAN_MASK), 0);
	}
	CloseHandle(hmap);

	if (base) {
	    WSABUF buf = {n, base + size_lo};

	    if (!WSASend(sd, &buf, 1, &res, 0, NULL, NULL)) {
		LONG off_hi = 0L;
		SetFilePointer(fd, res, &off_hi, SEEK_CUR);
	    }
	    UnmapViewOfFile(base);
	}
    }
    return res;
}

#endif


/*
 * Arguments: sd_udata, fd_udata, [count (number)]
 * Returns: [count (number) | false (EAGAIN)]
 */
static int
sock_sendfile (lua_State *L)
{
    sd_t sd = (sd_t) lua_unboxinteger(L, 1, SD_TYPENAME);
    fd_t fd = (fd_t) lua_unboxinteger(L, 2, FD_TYPENAME);
    size_t n = (size_t) lua_tointeger(L, 3);
    ssize_t res;

    sys_vm_leave();
#ifndef _WIN32
#if defined(__linux__)
    do res = sendfile(sd, fd, NULL, n ? n : ~((size_t) 0));
    while (res == -1 && SYS_ERRNO == EINTR);
#else
    {
	off_t nw, off = lseek(fd, 0, SEEK_CUR);

#if defined(__APPLE__) && defined(__MACH__)
	nw = n;
	do res = sendfile(fd, sd, off, &nw, NULL, 0);
#else
	do res = sendfile(fd, sd, off, n, NULL, &nw, 0);
#endif
	while (res == -1 && SYS_ERRNO == EINTR);
	if (res != -1) {
	    res = (size_t) nw;
	    lseek(fd, nw, SEEK_CUR);
	}
    }
#endif
    sys_vm_enter();

    if (res != -1 || SYS_EAGAIN(SYS_ERRNO)) {
	if (res == -1) {
	    lua_pushboolean(L, 0);
	    return 1;
	}
#else
    res = TransmitFileMap(sd, fd, n);
    sys_vm_enter();

    if (res != 0L) {
#endif
	lua_pushinteger(L, res);
	return 1;
    }
    return sys_seterror(L, 0);
}

/*
 * Arguments: sd_udata, {string | membuf_udata} ...
 * Returns: [success/partial (boolean), count (number)]
 */
static int
sock_write (lua_State *L)
{
    sd_t sd = (sd_t) lua_unboxinteger(L, 1, SD_TYPENAME);
    ssize_t n = 0;  /* number of chars actually write */
    int i, nargs = lua_gettop(L);

    for (i = 2; i <= nargs; ++i) {
	struct sys_buffer sb;
	int nw;

	if (!sys_buffer_read_init(L, i, &sb))
	    continue;
	sys_vm_leave();
#ifndef _WIN32
	do nw = write(sd, sb.ptr.r, sb.size);
	while (nw == -1 && SYS_ERRNO == EINTR);
#else
	{
	    WSABUF buf = {sb.size, sb.ptr.w};
	    DWORD l;
	    nw = !WSASend(sd, &buf, 1, &l, 0, NULL, NULL) ? l : -1;
	}
#endif
	sys_vm_enter();
	if (nw == -1) {
	    if (n > 0 || SYS_EAGAIN(SYS_ERRNO)) break;
	    return sys_seterror(L, 0);
	}
	n += nw;
	sys_buffer_read_next(&sb, nw);
	if ((size_t) nw < sb.size) break;
    }
    lua_pushboolean(L, (i > nargs));
    lua_pushinteger(L, n);
    return 2;
}

/*
 * Arguments: sd_udata, [membuf_udata, count (number)]
 * Returns: [string | false (EAGAIN)]
 */
static int
sock_read (lua_State *L)
{
    sd_t sd = (sd_t) lua_unboxinteger(L, 1, SD_TYPENAME);
    size_t n = !lua_isnumber(L, -1) ? ~((size_t) 0)
     : (size_t) lua_tointeger(L, -1);
    const size_t len = n;  /* how much total to read */
    size_t rlen;  /* how much to read */
    int nr;  /* number of bytes actually read */
    struct sys_buffer sb;
    char buf[SYS_BUFSIZE];

    sys_buffer_write_init(L, 2, &sb, buf, sizeof(buf));
    do {
	rlen = (n <= sb.size) ? n : sb.size;
	sys_vm_leave();
#ifndef _WIN32
	do nr = read(sd, sb.ptr.w, rlen);
	while (nr == -1 && SYS_ERRNO == EINTR);
#else
	{
	    WSABUF buf = {rlen, sb.ptr.w};
	    DWORD l, flags = 0;
	    nr = !WSARecv(sd, &buf, 1, &l, &flags, NULL, NULL) ? l : -1;
	}
#endif
	sys_vm_enter();
	if (nr == -1) break;
	n -= nr;  /* still have to read `n' bytes */
    } while ((n != 0L && nr == (int) rlen)  /* until end of count or eof */
     && sys_buffer_write_next(L, &sb, buf, 0));
    if (nr <= 0 && len == n) {
	if (!nr || !SYS_EAGAIN(SYS_ERRNO)) goto err;
	lua_pushboolean(L, 0);
    } else {
	if (!sys_buffer_write_done(L, &sb, buf, nr))
	    lua_pushinteger(L, len - n);
    }
    return 1;
 err:
    return sys_seterror(L, 0);
}

/*
 * Arguments: sd_udata
 * Returns: string
 */
static int
sock_tostring (lua_State *L)
{
    sd_t sd = (sd_t) lua_unboxinteger(L, 1, SD_TYPENAME);

    if (sd != (sd_t) -1)
	lua_pushfstring(L, SD_TYPENAME " (%d)", (int) sd);
    else
	lua_pushliteral(L, SD_TYPENAME " (closed)");
    return 1;
}


static luaL_reg sock_meth[] = {
    {"socket",		sock_socket},
    {"close",		sock_close},
    {"shutdown",	sock_shutdown},
    {"nonblocking",	sock_nonblocking},
    {"sockopt",		sock_sockopt},
    {"membership",	sock_membership},
    {"bind",		sock_bind},
    {"listen",		sock_listen},
    {"accept",		sock_accept},
    {"connect",		sock_connect},
    {"send",		sock_send},
    {"recv",		sock_recv},
    {"sendfile",	sock_sendfile},
    {"write",		sock_write},
    {"read",		sock_read},
    {"__tostring",	sock_tostring},
    {"__gc",		sock_close},
    {SYS_BUFIO_TAG,	NULL},  /* can operate with buffers */
    {NULL, NULL}
};

static luaL_reg sock_lib[] = {
    {"handle",		sock_new},
    ADDR_METHODS,
    {NULL, NULL}
};


#ifdef _WIN32

static int
sock_uninit (lua_State *L)
{
    (void) L;
    WSACleanup();
    return 0;
}

/*
 * Arguments: ..., sock_lib (table)
 */
static int
sock_init (lua_State *L)
{
    const WORD version = MAKEWORD(2, 2);
    WSADATA wsa;

    if (WSAStartup(version, &wsa))
	return -1;
    if (wsa.wVersion != version) {
	WSACleanup();
	return -1;
    }

    lua_newuserdata(L, 0);
    lua_newtable(L);  /* metatable */
    lua_pushvalue(L, -1);
    lua_pushcfunction(L, sock_uninit);
    lua_setfield(L, -2, "__gc");
    lua_setmetatable(L, -3);
    lua_rawset(L, -3);
    return 0;
}

#endif


LUALIB_API int
luaopen_sys_sock (lua_State *L)
{
    luaL_newmetatable(L, SD_TYPENAME);
    lua_pushvalue(L, -1);  /* push metatable */
    lua_setfield(L, -2, "__index");  /* metatable.__index = metatable */
    luaL_register(L, NULL, sock_meth);

    luaL_newmetatable(L, SA_TYPENAME);
    lua_pushvalue(L, -1);  /* push metatable */
    lua_setfield(L, -2, "__index");  /* metatable.__index = metatable */
    luaL_register(L, NULL, addr_meth);

    luaL_register(L, LUA_SOCKLIBNAME, sock_lib);

#ifdef _WIN32
    if (sock_init(L)) return 0;
#endif
    return 1;
}
