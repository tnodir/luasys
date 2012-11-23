/* Lua System: Randomness */

#ifdef _WIN32
#include <wincrypt.h>
#endif

#define RAND_TYPENAME	"sys.random"


/*
 * Returns: [rand_udata]
 */
static int
sys_random (lua_State *L)
{
#ifndef _WIN32
  fd_t *fdp = lua_newuserdata(L, sizeof(fd_t));

#if defined(__OpenBSD__)
  *fdp = open("/dev/arandom", O_RDONLY, 0);
  if (*fdp == (fd_t) -1)
    *fdp = open("/dev/urandom", O_RDONLY, 0);
#else
  *fdp = open("/dev/urandom", O_RDONLY, 0);
#endif
  if (*fdp != (fd_t) -1) {
#else
  HCRYPTPROV *p = lua_newuserdata(L, sizeof(void *));

  if (CryptAcquireContext(p, NULL, NULL,
   PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
#endif
    luaL_getmetatable(L, RAND_TYPENAME);
    lua_setmetatable(L, -2);
    return 1;
  }
  return sys_seterror(L, 0);
}

/*
 * Arguments: rand_udata
 */
static int
rand_close (lua_State *L)
{
#ifndef _WIN32
  fd_t *fdp = (fd_t *) checkudata(L, 1, RAND_TYPENAME);

  if (*fdp != (fd_t) -1) {
    close(*fdp);
    *fdp = (fd_t) -1;
  }
#else
  HCRYPTPROV *p = (HCRYPTPROV *) checkudata(L, 1, RAND_TYPENAME);

  if (*p != (HCRYPTPROV) -1) {
    CryptReleaseContext(*p, 0);
    *p = (HCRYPTPROV) -1;
  }
#endif
  return 0;
}

/*
 * Arguments: rand_udata, [upper_bound (number)
 *	| buffer (ludata), buffer_length (number)]
 * Returns: number
 */
static int
rand_next (lua_State *L)
{
  const int is_udata = lua_isuserdata(L, 2);
  const unsigned int ub = is_udata ? 0 : lua_tointeger(L, 2);
  unsigned int num;
  unsigned char *buf = is_udata ? lua_touserdata(L, 2) : &num;
  const int len = is_udata ? luaL_checkinteger(L, 3) : (int) sizeof(num);
#ifndef _WIN32
  fd_t fd = (fd_t) lua_unboxinteger(L, 1, RAND_TYPENAME);
  int nr;

  sys_vm_leave(L);
  do nr = read(fd, (char *) buf, len);
  while (nr == -1 && sys_eintr());
  sys_vm_enter(L);

  if (nr == len) {
#else
  HCRYPTPROV prov = (HCRYPTPROV) lua_unboxpointer(L, 1, RAND_TYPENAME);
  int res;

  sys_vm_leave(L);
  res = CryptGenRandom(prov, len, buf);
  sys_vm_enter(L);

  if (res) {
#endif
    lua_pushinteger(L, is_udata ? 1
     : (ub ? num % ub : num));
    return 1;
  }
  return sys_seterror(L, 0);
}


#define RAND_METHODS \
  {"random",		sys_random}

static luaL_Reg rand_meth[] = {
  {"__call",		rand_next},
  {"__gc",		rand_close},
  {NULL, NULL}
};
