/* Lua System: Networking: Addresses */

#ifdef _WIN32

#define in6_addr	in_addr6

#define IFF_POINTOPOINT	IFF_POINTTOPOINT

#else

#include <net/if.h>
#include <ifaddrs.h>

#endif /* !WIN32 */

#ifdef AI_CANONNAME
#define USE_GAI

#ifndef AI_ADDRCONFIG
#define AI_ADDRCONFIG	0
#endif
#endif


#define SA_TYPENAME	"sys.sock.addr"

struct sock_addr {
  union {
    struct sockaddr addr;
    struct sockaddr_in in;
#ifdef AF_INET6
    struct sockaddr_in6 in6;
#endif
#ifdef AF_LOCAL
    struct sockaddr_un un;
#endif
  } u;
  socklen_t addrlen;
};

#define SOCK_ADDR_LEN	offsetof(struct sock_addr, addrlen)

#define sock_addr_get_inp(sap, af) \
  ((af) == AF_INET ? (void *) &(sap)->u.in.sin_addr \
   : (void *) &(sap)->u.in6.sin6_addr)

#define sock_addr_get_inlen(af) \
  ((af) == AF_INET ? sizeof(struct in_addr) \
   : sizeof(struct in6_addr))


/*
 * Arguments: ..., binary_address (string)
 */
static const char *
sock_checkladdr (lua_State *L, int idx, int *in_lenp, int *afp)
{
  size_t len;
  const char *addr = luaL_checklstring(L, idx, &len);
  const int in_len = (len == 4) ? sizeof(struct in_addr)
   : sizeof(struct in6_addr);

  if ((int) len != in_len)
    luaL_argerror(L, idx, "invalid binary_address");

  *in_lenp = in_len;
  *afp = (in_len == 4) ? AF_INET : AF_INET6;
  return addr;
}

/*
 * Returns: binary_address (string)
 */
static void
sock_pushaddr (lua_State *L, struct sock_addr *sap)
{
  const int af = sap->u.addr.sa_family;
  const void *inp = sock_addr_get_inp(sap, af);
  const int in_len = sock_addr_get_inlen(af);

  lua_pushlstring(L, inp, in_len);
}


#ifdef USE_GAI

/*
 * Returns: nil, string
 */
static int
sock_gai_seterror (lua_State *L, int err)
{
  lua_pushnil(L);
  lua_pushstring(L, gai_strerror(err));
  lua_pushvalue(L, -1);
  lua_setglobal(L, SYS_ERROR_MESSAGE);
  return 2;
}

#else

#define sock_gai_seterror	sys_seterror

#endif


/*
 * Arguments: host_name (string), [binary_addresses (table)]
 * Returns: [binary_addresses (table) | binary_address (string),
 *	canon_name (string)]
 */
static int
sock_getaddrinfo (lua_State *L)
{
  const char *host = luaL_checkstring(L, 1);
  int gai_errno;

#ifdef USE_GAI
  struct addrinfo hints, *result;

  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_UNSPEC;
  hints.ai_flags = AI_ADDRCONFIG | AI_CANONNAME;

  sys_vm_leave(L);
  gai_errno = getaddrinfo(host, NULL, &hints, &result);
  sys_vm_enter(L);

  if (gai_errno) goto err;

  lua_settop(L, 2);
  if (!lua_istable(L, 2))
    sock_pushaddr(L, (struct sock_addr *) result->ai_addr);
  else {
    struct addrinfo *rp = result;
    int i;

    for (i = 1; rp; ++i, rp = rp->ai_next) {
      sock_pushaddr(L, (struct sock_addr *) rp->ai_addr);
      lua_rawseti(L, 2, i);
    }
  }
  lua_pushstring(L, result->ai_canonname);
  freeaddrinfo(result);
#else
  struct hostent *hp;

  sys_vm_leave(L);
  hp = gethostbyname(host);
  sys_vm_enter(L);

  if (!hp) {
    gai_errno = h_errno;
    goto err;
  }

  lua_settop(L, 2);
  if (!lua_istable(L, 2))
    lua_pushlstring(L, (char *) *hp->h_addr_list,
     sizeof(struct in_addr));
  else {
    const char **ap = hp->h_addr_list;
    int i;

    for (i = 1; *ap; ++i, ++ap) {
      lua_pushlstring(L, *ap, sizeof(struct in_addr));
      lua_rawseti(L, 2, i);
    }
  }
  lua_pushstring(L, hp->h_name);
#endif
  return 2;
 err:
  return sock_gai_seterror(L, gai_errno);
}

/*
 * Arguments: binary_address (string)
 * Returns: [host_name (string)]
 */
static int
sock_getnameinfo (lua_State *L)
{
  int len, af;
  const char *addr = sock_checkladdr(L, 1, &len, &af);
  struct sock_addr sa;
  void *inp = sock_addr_get_inp(&sa, af);
  const int in_len = sock_addr_get_inlen(af);
#ifdef USE_GAI
  char host[NI_MAXHOST];
#else
  struct hostent *hp;
#endif
  int gai_errno;

  memset(&sa, 0, sizeof(struct sock_addr));
  sa.u.addr.sa_family = (short) af;
  memcpy(inp, addr, in_len);

#ifdef USE_GAI
  sys_vm_leave(L);
  gai_errno = getnameinfo(&sa.u.addr, sizeof(struct sock_addr),
   host, sizeof(host), NULL, 0, NI_NAMEREQD);
  sys_vm_enter(L);

  if (gai_errno) goto err;

  lua_pushstring(L, host);
#else
  sys_vm_leave(L);
  hp = gethostbyaddr(inp, in_len, af);
  sys_vm_enter(L);

  if (!hp) {
    gai_errno = h_errno;
    goto err;
  }

  lua_pushstring(L, hp->h_name);
#endif
  return 1;
 err:
  return sock_gai_seterror(L, gai_errno);
}

/*
 * Returns: [interfaces (table)]
 */
static int
sock_getifaddrs (lua_State *L)
{
  struct sock_addr *sap;
  int i, res;

#ifndef _WIN32
  struct ifaddrs *result, *rp;

  sys_vm_leave(L);
  res = getifaddrs(&result);
  sys_vm_enter(L);
#else
  INTERFACE_INFO result[8192], *rp;
  SOCKET sd = WSASocketW(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAGS);
  DWORD n;

  sys_vm_leave(L);
  res = WSAIoctl(sd, SIO_GET_INTERFACE_LIST, NULL, 0,
   result, sizeof(result), &n, NULL, NULL);

  closesocket(sd);
  sys_vm_enter(L);
#endif

  if (res == -1)
    return sys_seterror(L, 0);

  lua_createtable(L, 8, 0);
  rp = result;
#ifndef _WIN32
  for (i = 0; rp; rp = rp->ifa_next) {
#else
  for (i = 0; n--; ++rp) {
#endif
#ifndef _WIN32
    sap = (struct sock_addr *) rp->ifa_addr;
#else
    sap = (struct sock_addr *) &rp->iiAddress;
#endif
    if (!sap || sap->u.addr.sa_family == AF_UNSPEC)
      continue;

    lua_newtable(L);
    {
      const int af = sap->u.addr.sa_family;

      if (af == AF_INET
#ifdef AF_INET6
       || af == AF_INET6
#endif
      ) {
        sock_pushaddr(L, sap);
        lua_setfield(L, -2, "addr");
      }

      {
        const char *s = NULL;

        switch (af) {
        case AF_INET: s = "INET"; break;
#ifdef AF_INET6
        case AF_INET6: s = "INET6"; break;
#endif
#ifdef AF_LOCAL
        case AF_LOCAL: s = "LOCAL"; break;
#endif
#ifdef AF_AX25
        case AF_AX25: s = "AX25"; break;
#endif
#ifdef AF_IPX
        case AF_IPX: s = "IPX"; break;
#endif
#ifdef AF_APPLETALK
        case AF_APPLETALK: s = "APPLETALK"; break;
#endif
#ifdef AF_NETROM
        case AF_NETROM: s = "NETROM"; break;
#endif
#ifdef AF_BRIDGE
        case AF_BRIDGE: s = "BRIDGE"; break;
#endif
#ifdef AF_ATMPVC
        case AF_ATMPVC: s = "ATMPVC"; break;
#endif
#ifdef AF_X25
        case AF_X25: s = "X25"; break;
#endif
#ifdef AF_ROSE
        case AF_ROSE: s = "ROSE"; break;
#endif
#ifdef AF_DECnet
        case AF_DECnet: s = "DECnet"; break;
#endif
#ifdef AF_NETBEUI
        case AF_NETBEUI: s = "NETBEUI"; break;
#endif
#ifdef AF_SECURITY
        case AF_SECURITY: s = "SECURITY"; break;
#endif
#ifdef AF_KEY
        case AF_KEY: s = "KEY"; break;
#endif
#ifdef AF_NETLINK
        case AF_NETLINK: s = "NETLINK"; break;
#endif
#ifdef AF_PACKET
        case AF_PACKET: s = "PACKET"; break;
#endif
#ifdef AF_ASH
        case AF_ASH: s = "ASH"; break;
#endif
#ifdef AF_ECONET
        case AF_ECONET: s = "ECONET"; break;
#endif
#ifdef AF_ATMSVC
        case AF_ATMSVC: s = "ATMSVC"; break;
#endif
#ifdef AF_RDS
        case AF_RDS: s = "RDS"; break;
#endif
#ifdef AF_SNA
        case AF_SNA: s = "SNA"; break;
#endif
#ifdef AF_IRDA
        case AF_IRDA: s = "IRDA"; break;
#endif
#ifdef AF_PPPOX
        case AF_PPPOX: s = "PPPOX"; break;
#endif
#ifdef AF_WANPIPE
        case AF_WANPIPE: s = "WANPIPE"; break;
#endif
#ifdef AF_LLC
        case AF_LLC: s = "LLC"; break;
#endif
#ifdef AF_CAN
        case AF_CAN: s = "CAN"; break;
#endif
#ifdef AF_TIPC
        case AF_TIPC: s = "TIPC"; break;
#endif
#ifdef AF_BLUETOOTH
        case AF_BLUETOOTH: s = "BLUETOOTH"; break;
#endif
#ifdef AF_IUCV
        case AF_IUCV: s = "IUCV"; break;
#endif
#ifdef AF_RXRPC
        case AF_RXRPC: s = "RXRPC"; break;
#endif
#ifdef AF_ISDN
        case AF_ISDN: s = "ISDN"; break;
#endif
#ifdef AF_PHONET
        case AF_PHONET: s = "PHONET"; break;
#endif
#ifdef AF_IEEE802154
        case AF_IEEE802154: s = "IEEE802154"; break;
#endif
        default: s = "UNKNOWN";
        }
        if (s) {
          lua_pushstring(L, s);
          lua_setfield(L, -2, "family");
        }
      }

#ifndef _WIN32
      sap = (struct sock_addr *) rp->ifa_netmask;
#else
      sap = (struct sock_addr *) &rp->iiNetmask;
#endif
      if (sap) {
        sock_pushaddr(L, sap);
        lua_setfield(L, -2, "netmask");
      }

#ifndef _WIN32
      sap = (struct sock_addr *) rp->ifa_broadaddr;
#else
      sap = (struct sock_addr *) &rp->iiBroadcastAddress;
#endif
      if (sap) {
        sock_pushaddr(L, sap);
        lua_setfield(L, -2, "broadaddr");
      }

      lua_createtable(L, 0, 5);
      {
#ifndef _WIN32
        const int flags = rp->ifa_flags;
#else
        const int flags = rp->iiFlags;
#endif

        lua_pushboolean(L, flags & IFF_UP);
        lua_setfield(L, -2, "up");

        lua_pushboolean(L, flags & IFF_BROADCAST);
        lua_setfield(L, -2, "broadcast");

        lua_pushboolean(L, flags & IFF_LOOPBACK);
        lua_setfield(L, -2, "loopback");

        lua_pushboolean(L, flags & IFF_POINTOPOINT);
        lua_setfield(L, -2, "pointtopoint");

        lua_pushboolean(L, flags & IFF_MULTICAST);
        lua_setfield(L, -2, "multicast");
      }
      lua_setfield(L, -2, "flags");
    }
    lua_rawseti(L, -2, ++i);
  }
#ifndef _WIN32
  freeifaddrs(result);
#endif
  return 1;
}


/*
 * Arguments: text_address (string), [ip4_tonumber (true)]
 * Returns: [binary_address (string | number)]
 */
static int
sock_inet_pton (lua_State *L)
{
  const char *src = luaL_checkstring(L, 1);
  const int to_ip4 = lua_toboolean(L, 2);
  const int af = (!to_ip4 && strchr(src, ':')) ? AF_INET6 : AF_INET;
  struct sock_addr sa;
  void *inp = sock_addr_get_inp(&sa, af);
  const int in_len = sock_addr_get_inlen(af);
#ifdef _WIN32
  union sys_rwptr src_ptr;  /* to avoid "const cast" warning */
#endif

  memset(&sa, 0, sizeof(struct sock_addr));
  if (*src == '*') goto end;

#ifndef _WIN32
  if (inet_pton(af, src, inp) == 1) {
#else
  sa.addrlen = sizeof(sa);
  src_ptr.r = src;
  if (!WSAStringToAddressA(src_ptr.w, af, NULL,
   &sa.u.addr, &sa.addrlen)) {
#endif
 end:
    if (to_ip4)
      lua_pushnumber(L, ntohl(*((unsigned long *) inp)));
    else
      lua_pushlstring(L, inp, in_len);
    return 1;
  }
  return sys_seterror(L, 0);
}

/*
 * Arguments: binary_address (string | number)
 * Returns: [text_address (string)]
 */
static int
sock_inet_ntop (lua_State *L)
{
  const int is_ip4 = (lua_type(L, 1) == LUA_TNUMBER);
  unsigned long ip4;
  int in_len, af;
  const char *src;
  char buf[48];

  if (is_ip4) {
    const lua_Number num = lua_tonumber(L, 1);

    in_len = 4;
    af = AF_INET;
    ip4 = htonl((unsigned long) num);
    src = (const char *) &ip4;
  } else {
    src = sock_checkladdr(L, 1, &in_len, &af);
  }

#ifndef _WIN32
  if (inet_ntop(af, src, buf, sizeof(buf)) == NULL)
    goto err;
#else
  {
    struct sock_addr sa;
    void *inp = sock_addr_get_inp(&sa, af);
    const int sl = (af == AF_INET) ? sizeof(struct sockaddr_in)
     : sizeof(struct sockaddr_in6);
    DWORD buflen = sizeof(buf);

    memset(&sa, 0, sizeof(struct sock_addr));
    memcpy(inp, src, in_len);
    sa.u.addr.sa_family = (short) af;

    if (WSAAddressToStringA(&sa.u.addr, sl, NULL, buf, &buflen)
     || buflen >= sizeof(buf))
      goto err;
  }
#endif
  lua_pushstring(L, buf);
  return 1;
 err:
  return sys_seterror(L, 0);
}


/*
 * Returns: sock_addr_udata
 */
static int
sock_addr_new (lua_State *L)
{
  lua_newuserdata(L, sizeof(struct sock_addr));
  luaL_getmetatable(L, SA_TYPENAME);
  lua_setmetatable(L, -2);
  return 1;
}

/*
 * Arguments: sock_addr_udata, [port (number), binary_address (string)]
 * Returns: sock_addr_udata | port (number), binary_address (string)
 */
static int
sock_addr_inet (lua_State *L)
{
  struct sock_addr *sap = checkudata(L, 1, SA_TYPENAME);

  if (lua_gettop(L) == 1) {
    const int af = sap->u.addr.sa_family;

    if (af == AF_INET) {
      lua_pushinteger(L, ntohs(sap->u.in.sin_port));
      lua_pushlstring(L, (char *) &sap->u.in.sin_addr,
       sizeof(struct in_addr));
    } else if (af == AF_INET6) {
      lua_pushinteger(L, ntohs(sap->u.in6.sin6_port));
      lua_pushlstring(L, (char *) &sap->u.in6.sin6_addr,
       sizeof(struct in6_addr));
    } else
      return 0;
    return 2;
  } else {
    const int port = lua_tointeger(L, 2);
    int in_len = SOCK_ADDR_LEN, af = AF_INET;
    const char *addr = lua_isnoneornil(L, 3) ? NULL
     : sock_checkladdr(L, 3, &in_len, &af);

    memset(sap, 0, sizeof(struct sock_addr));
    sap->u.addr.sa_family = (short) af;
    if (af == AF_INET) {
      sap->u.in.sin_port = htons((unsigned short) port);
      if (addr)
        memcpy(&sap->u.in.sin_addr, addr, in_len);
      sap->addrlen = sizeof(struct sockaddr_in);
    } else {
      sap->u.in6.sin6_port = htons((unsigned short) port);
      if (addr)
        memcpy(&sap->u.in6.sin6_addr, addr, in_len);
      sap->addrlen = sizeof(struct sockaddr_in6);
    }
    lua_settop(L, 1);
    return 1;
  };
}

/*
 * Arguments: sock_addr_udata, [path (string)]
 * Returns: sock_addr_udata | path (string)
 */
static int
sock_addr_file (lua_State *L)
{
  struct sock_addr *sap = checkudata(L, 1, SA_TYPENAME);

#ifndef _WIN32
  if (lua_gettop(L) == 1) {
    if (sap->u.addr.sa_family == AF_LOCAL) {
      lua_pushstring(L, sap->u.un.sun_path);
      return 1;
    }
  } else {
    size_t len;
    const char *path = luaL_checklstring(L, 2, &len);

    if (len < sizeof(sap->u.un.sun_path)) {
      sap->u.un.sun_family = AF_LOCAL;
      sap->addrlen = ++len;
      memcpy(sap->u.un.sun_path, path, len);

      lua_settop(L, 1);
      return 1;
    }
  };
#else
  (void) sap;
#endif
  return 0;
}

/*
 * Arguments: sock_addr_udata, sd_udata
 * Returns: [sock_addr_udata]
 */
static int
sock_addr_getsockname (lua_State *L)
{
  struct sock_addr *sap = checkudata(L, 1, SA_TYPENAME);
  sd_t sd = (sd_t) lua_unboxinteger(L, 2, SD_TYPENAME);

  sap->addrlen = SOCK_ADDR_LEN;
  if (!getsockname(sd, &sap->u.addr, &sap->addrlen)) {
    lua_settop(L, 1);
    return 1;
  }
  return sys_seterror(L, 0);
}

/*
 * Arguments: sock_addr_udata, sd_udata
 * Returns: [sock_addr_udata]
 */
static int
sock_addr_getpeername (lua_State *L)
{
  struct sock_addr *sap = checkudata(L, 1, SA_TYPENAME);
  sd_t sd = (sd_t) lua_unboxinteger(L, 2, SD_TYPENAME);

  sap->addrlen = SOCK_ADDR_LEN;
  if (!getpeername(sd, &sap->u.addr, &sap->addrlen)) {
    lua_settop(L, 1);
    return 1;
  }
  return sys_seterror(L, 0);
}

/*
 * Arguments: sock_addr_udata
 * Returns: string
 */
static int
sock_addr_tostring (lua_State *L)
{
  struct sock_addr *sap = checkudata(L, 1, SA_TYPENAME);

  lua_pushfstring(L, SA_TYPENAME " (%p)", sap);
  return 1;
}


#define ADDR_METHODS \
  {"getaddrinfo",	sock_getaddrinfo}, \
  {"getnameinfo",	sock_getnameinfo}, \
  {"getifaddrs",	sock_getifaddrs}, \
  {"inet_pton",		sock_inet_pton}, \
  {"inet_ntop",		sock_inet_ntop}, \
  {"addr",		sock_addr_new}

static luaL_Reg addr_meth[] = {
  {"inet",		sock_addr_inet},
  {"file",		sock_addr_file},
  {"getsockname",	sock_addr_getsockname},
  {"getpeername",	sock_addr_getpeername},
  {"__tostring",	sock_addr_tostring},
  {NULL, NULL}
};
