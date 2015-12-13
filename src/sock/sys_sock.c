/* Lua System: Networking */

#include "../common.h"

#ifdef _WIN32

#include <ws2tcpip.h>	/* Multicast */

#ifndef WSA_FLAG_NO_HANDLE_INHERIT
#define WSA_FLAG_NO_HANDLE_INHERIT	0
#endif

#define WSA_FLAGS	(WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT)

#define SHUT_WR		SD_SEND

#define SO_LOOPBACK	0x98000010  /* SIO_LOOPBACK_FAST_PATH */

typedef int		socklen_t;

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

#ifndef TCP_FASTOPEN
#define TCP_FASTOPEN	23
#endif

#ifndef MSG_FASTOPEN
#define MSG_FASTOPEN	0x20000000
#endif


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

  if ((sd = WSASocketW(AF_INET, type, 0, NULL, 0, WSA_FLAGS))
   != INVALID_SOCKET) {
    int optval = 1;
    DWORD nr;

    WSAIoctl(sd, SO_LOOPBACK, &optval, sizeof(int), NULL, 0, &nr, 0, 0);

    if (!bind(sd, (struct sockaddr *) &sa, len)
     && !listen(sd, 1)
     && !getsockname(sd, (struct sockaddr *) &sa, &len)
     && (sv[0] = WSASocketW(AF_INET, type, 0, NULL, 0, WSA_FLAGS))
      != INVALID_SOCKET) {
      struct sockaddr_in sa2;
      int len2 = sizeof(struct sockaddr_in);

      WSAIoctl(sv[0], SO_LOOPBACK, &optval, sizeof(int), NULL, 0, &nr, 0, 0);

      sv[1] = (sd_t) -1;
      if (!connect(sv[0], (struct sockaddr *) &sa, len)
       && (sv[1] = accept(sd, (struct sockaddr *) &sa, &len))
        != INVALID_SOCKET
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
 * Arguments: sd_udata, [type ("stream", "dgram", "raw"),
 *	domain ("inet", "inet6", "unix"), sd_udata (socketpair)]
 * Returns: [sd_udata]
 */
static int
sock_socket (lua_State *L)
{
  sd_t *sdp = checkudata(L, 1, SD_TYPENAME);
  const char *typep = lua_tostring(L, 2);
  const char *domainp = lua_tostring(L, 3);
  int type = SOCK_STREAM, domain = AF_INET, proto = 0;
  sd_t *pair_sdp = (lua_gettop(L) > 1 && lua_isuserdata(L, -1))
   ? checkudata(L, -1, SD_TYPENAME) : NULL;

  if (typep) {
    if (typep[0] == 'd')
      type = SOCK_DGRAM;
    else if (typep[0] == 'r') {
      type = SOCK_RAW;
      proto = IPPROTO_RAW;
    }
  }
  if (domainp) {
    if (domainp[0] == 'u')
      domain = AF_UNIX;
    else if (domainp[0] == 'i' && domainp[1] == 'n' && domainp[2] == 'e'
     && domainp[3] == 't' && domainp[4] == '6')
      domain = AF_INET6;
  }

  lua_settop(L, 1);
  if (pair_sdp) {
    sd_t sv[2];
#ifndef _WIN32
    if (!socketpair(AF_UNIX, type, 0, sv)) {
#else
    if (!sock_pair(type, sv)) {
#endif
      *sdp = sv[0];
      *pair_sdp = sv[1];
      return 1;
    }
  } else {
    sd_t sd;
#ifndef _WIN32
    sd = socket(domain, type, proto);
#else
    sd = WSASocketW(domain, type, proto, NULL, 0, WSA_FLAGS);
#endif
    if (sd != (sd_t) -1) {
      *sdp = sd;
      return 1;
    }
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
  int res;

  if (*sdp != (sd_t) -1) {
    sys_vm_leave(L);
#ifndef _WIN32
    do res = close(*sdp);
    while (res == -1 && sys_eintr());
#else
    res = closesocket(*sdp);
#endif
    sys_vm_enter(L);
    *sdp = (sd_t) -1;
    lua_pushboolean(L, !res);
    return 1;
  }
  return 0;
}

/*
 * Arguments: sd_udata, [handle (ludata)]
 * Returns: [sd_udata | handle (ludata)]
 */
static int
sock_handle (lua_State *L)
{
  sd_t *sdp = checkudata(L, 1, SD_TYPENAME);

  if (lua_gettop(L) > 1) {
    void *h = lua_touserdata(L, 2);
    *sdp = !h ? (sd_t) -1 : (sd_t) (size_t) h;
    lua_settop(L, 1);
  } else {
    if (*sdp == (sd_t) -1) lua_pushnil(L);
    else lua_pushlightuserdata(L, (void *) (size_t) *sdp);
  }
  return 1;
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

#define OPT_START	2
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
#define OPT_INDEX_TCP	12
    TCP_NODELAY, TCP_FASTOPEN,
#define OPT_INDEX_IP	14
    IP_MULTICAST_TTL, IP_MULTICAST_IF, IP_MULTICAST_LOOP,
    IP_HDRINCL
  };
  static const char *const opt_names[] = {
    "reuseaddr", "type", "error", "dontroute",
    "sndbuf", "rcvbuf", "sndlowat", "rcvlowat",
    "broadcast", "keepalive", "oobinline", "linger",
    "tcp_nodelay", "tcp_fastopen",
    "multicast_ttl", "multicast_if", "multicast_loop",
    "hdrincl", NULL
  };

  sd_t sd = (sd_t) lua_unboxinteger(L, 1, SD_TYPENAME);
  const int optname = luaL_checkoption(L, OPT_START, NULL, opt_names);
  const int level = (optname < OPT_INDEX_TCP) ? SOL_SOCKET
   : (optname < OPT_INDEX_IP ? IPPROTO_TCP : IPPROTO_IP);
  int optflag = opt_flags[optname];
  int optval[4];
  socklen_t optlen = sizeof(int);
  const int narg = lua_gettop(L);
#ifdef _WIN32
  int is_success = 0;
#endif

#if defined(_WIN32) || defined(SO_REUSEPORT)
  if (optflag == SO_REUSEADDR) {
    int so_type = -1;
    getsockopt(sd, SOL_SOCKET, SO_TYPE, (char *) &so_type, &optlen);
#ifdef _WIN32
    is_success = (so_type == SOCK_STREAM);
#else
    if (so_type == SOCK_DGRAM)
      optflag = SO_REUSEPORT;
#endif
  }
#endif

  if (narg > OPT_START) {
#ifdef _WIN32
    if (is_success) goto set_success;
#endif
    optval[0] = lua_tointeger(L, OPT_START + 1);
    if (narg > OPT_START + 1) {
      optval[1] = lua_tointeger(L, OPT_START + 2);
      optlen *= 2;
    }
    if (!setsockopt(sd, level, optflag, (char *) &optval, optlen)) {
#ifdef _WIN32
 set_success:
#endif
      lua_settop(L, 1);
      return 1;
    }
  } else {
#ifdef _WIN32
    if (is_success) {
      optval[0] = 1;
      goto get_success;
    }
#endif
    if (!getsockopt(sd, level, optflag, (char *) &optval, &optlen)) {
#ifdef _WIN32
 get_success:
#endif
      lua_pushinteger(L, optval[0]);
      if (optlen <= (socklen_t) sizeof(int))
        return 1;
      lua_pushinteger(L, optval[1]);
      return 2;
    }
  }
  return sys_seterror(L, 0);
}
#undef OPT_INDEX_TCP
#undef OPT_INDEX_IP
#undef OPT_START

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
  } else {
#ifdef IPPROTO_IPV6
    memcpy(&mr.ip6.ipv6mr_multiaddr, maddrp, len);
    mr.ip6.ipv6mr_interface = lua_tointeger(L, 3);

    level = IPPROTO_IPV6;
    mr_len = sizeof(struct ipv6_mreq);
#else
    luaL_argerror(L, 2, "invalid family");
    return 0;
#endif
  }

  if (!setsockopt(sd, level, optflag, (char *) &mr, mr_len)) {
    lua_settop(L, 1);
    return 1;
  }
  return sys_seterror(L, 0);
}

/*
 * Arguments: sd_udata, [loopback (boolean)]
 * Returns: [sd_udata]
 */
static int
sock_loopback (lua_State *L)
{
#ifdef _WIN32
  sd_t sd = (sd_t) lua_unboxinteger(L, 1, SD_TYPENAME);
  int optval = lua_isnoneornil(L, 2) || lua_toboolean(L, 2);
  DWORD nr;

  if (WSAIoctl(sd, SO_LOOPBACK, &optval, sizeof(int), NULL, 0, &nr, 0, 0))
    return sys_seterror(L, 0);
#endif

  lua_settop(L, 1);
  return 1;
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
    *slp = SOCK_ADDR_LEN;
  }

  sys_vm_leave(L);
#ifndef _WIN32
  do sd = accept(sd, sap, slp);
  while (sd == -1 && sys_eintr());
#else
  sd = accept(sd, sap, slp);
#endif
  sys_vm_enter(L);

  if (sd != (sd_t) -1) {
    *sdp = sd;
    lua_settop(L, 2);
    return 1;
  } else if (SYS_IS_EAGAIN(SYS_ERRNO)) {
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

  sys_vm_leave(L);
  do res = connect(sd, &sap->u.addr, sap->addrlen);
#ifndef _WIN32
  while (res == -1 && sys_eintr());
#else
  while (0);
#endif
  sys_vm_enter(L);

  if (!res) {
    lua_settop(L, 1);
    return 1;
  }
  if (SYS_IS_EAGAIN(SYS_ERRNO)
#ifndef _WIN32
   || SYS_ERRNO == EINPROGRESS || SYS_ERRNO == EALREADY
#else
   || SYS_ERRNO == WSAEINPROGRESS || SYS_ERRNO == WSAEALREADY
#endif
#if defined(__FreeBSD__)
   || SYS_ERRNO == EADDRINUSE
#endif
  ) {
    lua_pushboolean(L, 0);
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
    MSG_OOB, MSG_DONTROUTE, MSG_FASTOPEN
  };
  static const char *const o_names[] = {
    "oob", "dontroute", "fastopen", NULL
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
  sys_vm_leave(L);
  do nw = !to ? send(sd, sb.ptr.r, sb.size, flags)
   : sendto(sd, sb.ptr.r, sb.size, flags, &to->u.addr, to->addrlen);
#ifndef _WIN32
  while (nw == -1 && sys_eintr());
#else
  while (0);
#endif
  sys_vm_enter(L);
  if (nw == -1) {
    if (!SYS_IS_EAGAIN(SYS_ERRNO))
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
  struct sys_thread *td = sys_thread_get();
  struct sys_buffer sb;
  char buf[SYS_BUFSIZE];
  unsigned int i, flags = 0;
  int res = 0;

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
    if (td) sys_vm2_leave(td);
#ifndef _WIN32
    do nr = recvfrom(sd, sb.ptr.w, rlen, flags, sap, slp);
    while (nr == -1 && sys_eintr());
#else
    nr = recvfrom(sd, sb.ptr.w, rlen, flags, sap, slp);
#endif
    if (td) sys_vm2_enter(td);
    if (nr == -1) break;
    n -= nr;  /* still have to read 'n' bytes */
  } while ((n != 0L && nr == (int) rlen)  /* until end of count or eof */
   && sys_buffer_write_next(L, &sb, buf, 0));
  if (nr <= 0 && len == n) {
    if (nr && SYS_IS_EAGAIN(SYS_ERRNO))
      lua_pushboolean(L, 0);
    else res = -1;
  } else {
    if (!sys_buffer_write_done(L, &sb, buf, nr))
      lua_pushinteger(L, len - n);
  }
  if (td) sys_thread_check(td, L);
  if (!res) return 1;
  if (!nr) return 0;
  return sys_seterror(L, 0);
}


#ifdef _WIN32

#define USE_SENDFILE

#define SENDFILE_MAX	(16 * 1024 * 1024)
#define SYS_GRAN_MASK	(64 * 1024 - 1)

static DWORD
TransmitFileMap (SOCKET sd, HANDLE fd, DWORD n)
{
  HANDLE hmap = CreateFileMapping(fd, NULL, PAGE_READONLY, 0, 0, NULL);
  DWORD res = 0;

  if (hmap) {
    DWORD size_hi, size_lo, map_off;
    char *base = NULL;

    size_lo = GetFileSize(fd, &size_hi);
    if (size_lo != (DWORD) -1L || SYS_ERRNO == NO_ERROR) {
      LONG off_hi = 0L, off_lo;
      int64_t size;

      off_lo = SetFilePointer(fd, 0, &off_hi, SEEK_CUR);
      size = INT64_MAKE(size_lo, size_hi) - INT64_MAKE(off_lo, off_hi);

      if (size > (int64_t) SENDFILE_MAX)
        size = (int64_t) SENDFILE_MAX;

      if (n == 0 || n > (DWORD) size)
        n = (DWORD) size;

      base = MapViewOfFile(hmap, FILE_MAP_READ,
       off_hi, (off_lo & ~SYS_GRAN_MASK), 0);

      map_off = (off_lo & SYS_GRAN_MASK);
    }
    CloseHandle(hmap);

    if (base) {
      WSABUF wsa_buf;

      wsa_buf.len = n;
      wsa_buf.buf = base + map_off;

      if (!WSASend(sd, &wsa_buf, 1, &res, 0, NULL, NULL)) {
        LONG off_hi = 0L;
        SetFilePointer(fd, res, &off_hi, SEEK_CUR);
      }
      UnmapViewOfFile(base);
    }
  }
  return res;
}

#elif defined(__linux__) || defined(__FreeBSD__) \
   || (defined(__APPLE__) && defined(__MACH__))

#define USE_SENDFILE

#endif


#ifdef USE_SENDFILE

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

  sys_vm_leave(L);
#ifndef _WIN32
#if defined(__linux__)
  do res = sendfile(sd, fd, NULL, n ? n : ~((size_t) 0));
  while (res == -1 && sys_eintr());
#else
  {
    off_t nw, off = lseek(fd, 0, SEEK_CUR);

#if defined(__APPLE__) && defined(__MACH__)
    nw = n;
    do res = sendfile(fd, sd, off, &nw, NULL, 0);
#else
    do res = sendfile(fd, sd, off, n, NULL, &nw, 0);
#endif
    while (res == -1 && sys_eintr());
    if (res != -1) {
      res = (size_t) nw;
      lseek(fd, nw, SEEK_CUR);
    }
  }
#endif
  sys_vm_enter(L);

  if (res != -1 || SYS_IS_EAGAIN(SYS_ERRNO)) {
    if (res == -1) {
      lua_pushboolean(L, 0);
      return 1;
    }
#else
  res = TransmitFileMap(sd, fd, n);
  sys_vm_enter(L);

  if (res != 0L) {
#endif
    lua_pushinteger(L, res);
    return 1;
  }
  return sys_seterror(L, 0);
}

#endif


/*
 * Arguments: sd_udata, {string | membuf_udata} ...
 * Returns: [success/partial (boolean), count (number)]
 */
static int
sock_write (lua_State *L)
{
  sd_t sd = (sd_t) lua_unboxinteger(L, 1, SD_TYPENAME);
  ssize_t n = 0;  /* number of chars actually write */
  int i, narg = lua_gettop(L);

  for (i = 2; i <= narg; ++i) {
    struct sys_buffer sb;
    int nw;

    if (!sys_buffer_read_init(L, i, &sb))
      continue;
    sys_vm_leave(L);
#ifndef _WIN32
    do nw = write(sd, sb.ptr.r, sb.size);
    while (nw == -1 && sys_eintr());
#else
    {
      WSABUF wsa_buf;
      DWORD l;

      wsa_buf.len = sb.size;
      wsa_buf.buf = sb.ptr.w;

      nw = !WSASend(sd, &wsa_buf, 1, &l, 0, NULL, NULL)
       ? (int) l : -1;
    }
#endif
    sys_vm_enter(L);
    if (nw == -1) {
      if (n > 0 || SYS_IS_EAGAIN(SYS_ERRNO))
        break;
      return sys_seterror(L, 0);
    }
    n += nw;
    sys_buffer_read_next(&sb, nw);
    if ((size_t) nw < sb.size) break;
  }
  lua_pushboolean(L, (i > narg));
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
  struct sys_thread *td = sys_thread_get();
  struct sys_buffer sb;
  char buf[SYS_BUFSIZE];
  int res = 0;

  sys_buffer_write_init(L, 2, &sb, buf, sizeof(buf));
  do {
    rlen = (n <= sb.size) ? n : sb.size;
    if (td) sys_vm2_leave(td);
#ifndef _WIN32
    do nr = read(sd, sb.ptr.w, rlen);
    while (nr == -1 && sys_eintr());
#else
    {
      WSABUF wsa_buf;
      DWORD l, flags = 0;

      wsa_buf.len = rlen;
      wsa_buf.buf = sb.ptr.w;

      nr = !WSARecv(sd, &wsa_buf, 1, &l, &flags, NULL, NULL)
       ? (int) l : -1;
    }
#endif
    if (td) sys_vm2_enter(td);
    if (nr == -1) break;
    n -= nr;  /* still have to read 'n' bytes */
  } while ((n != 0L && nr == (int) rlen)  /* until end of count or eof */
   && sys_buffer_write_next(L, &sb, buf, 0));
  if (nr <= 0 && len == n) {
    if (nr && SYS_IS_EAGAIN(SYS_ERRNO))
      lua_pushboolean(L, 0);
    else res = -1;
  } else {
    if (!sys_buffer_write_done(L, &sb, buf, nr))
      lua_pushinteger(L, len - n);
  }
  if (td) sys_thread_check(td, L);
  if (!res) return 1;
  if (!nr) return 0;
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


static luaL_Reg sock_meth[] = {
  {"socket",		sock_socket},
  {"close",		sock_close},
  {"handle",		sock_handle},
  {"shutdown",		sock_shutdown},
  {"nonblocking",	sock_nonblocking},
  {"sockopt",		sock_sockopt},
  {"membership",	sock_membership},
  {"loopback",		sock_loopback},
  {"bind",		sock_bind},
  {"listen",		sock_listen},
  {"accept",		sock_accept},
  {"connect",		sock_connect},
  {"send",		sock_send},
  {"recv",		sock_recv},
#ifdef USE_SENDFILE
  {"sendfile",		sock_sendfile},
#endif
  {"write",		sock_write},
  {"read",		sock_read},
  {"__tostring",	sock_tostring},
  {"__gc",		sock_close},
  {SYS_BUFIO_TAG,	NULL},  /* can operate with buffers */
  {NULL, NULL}
};

static luaL_Reg sock_lib[] = {
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
  luaL_register(L, LUA_SOCKLIBNAME, sock_lib);
#ifdef _WIN32
  if (sock_init(L)) return 0;
#endif

  luaL_newmetatable(L, SD_TYPENAME);
  lua_pushvalue(L, -1);  /* push metatable */
  lua_setfield(L, -2, "__index");  /* metatable.__index = metatable */
  luaL_setfuncs(L, sock_meth, 0);
  lua_pop(L, 1);

  luaL_newmetatable(L, SA_TYPENAME);
  lua_pushvalue(L, -1);  /* push metatable */
  lua_setfield(L, -2, "__index");  /* metatable.__index = metatable */
  luaL_setfuncs(L, addr_meth, 0);
  lua_pop(L, 1);

  return 1;
}
