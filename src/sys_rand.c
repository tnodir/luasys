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

    if (CryptAcquireContext(p, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
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
 * Arguments: rand_udata, [upper_bound (number)]
 * Returns: number
 */
static int
rand_next (lua_State *L)
{
    unsigned int num, ub = lua_tointeger(L, 2);
#ifndef _WIN32
    fd_t fd = (fd_t) lua_unboxinteger(L, 1, RAND_TYPENAME);
    int nr;

    do nr = read(fd, (char *) &num, sizeof(num));
    while (nr == -1 && SYS_ERRNO == EINTR);
    if (nr == sizeof(num)) {
#else
    HCRYPTPROV prov = (HCRYPTPROV) lua_unboxpointer(L, 1, RAND_TYPENAME);

    if (CryptGenRandom(prov, sizeof(num), (unsigned char *) &num)) {
#endif
	lua_pushnumber(L, ub ? num % ub : num);
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
