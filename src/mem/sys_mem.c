/* Lua System: Memory Buffers */

#ifdef _WIN32

#define SYSMEM_HAVE_MMAP

#else

#include <sys/stat.h>
#include <sys/mman.h>

#ifdef _POSIX_MAPPED_FILES
#define SYSMEM_HAVE_MMAP
#endif

#endif


#define MEM_TYPENAME	"sys.mem.pointer"

#define BUFF_INITIALSIZE	128

struct membuf {
    char *data;
    int len, offset;

#define SYSMEM_TYPE_SHIFT	8
#define SYSMEM_TCHAR		((0  << SYSMEM_TYPE_SHIFT) | sizeof(char))
#define SYSMEM_TUCHAR		((1  << SYSMEM_TYPE_SHIFT) | sizeof(unsigned char))
#define SYSMEM_TSHORT		((2  << SYSMEM_TYPE_SHIFT) | sizeof(short))
#define SYSMEM_TUSHORT		((3  << SYSMEM_TYPE_SHIFT) | sizeof(unsigned short))
#define SYSMEM_TINT		((4  << SYSMEM_TYPE_SHIFT) | sizeof(int))
#define SYSMEM_TUINT		((5  << SYSMEM_TYPE_SHIFT) | sizeof(unsigned int))
#define SYSMEM_TLONG		((6  << SYSMEM_TYPE_SHIFT) | sizeof(long))
#define SYSMEM_TULONG		((7  << SYSMEM_TYPE_SHIFT) | sizeof(unsigned long))
#define SYSMEM_TFLOAT		((8  << SYSMEM_TYPE_SHIFT) | sizeof(float))
#define SYSMEM_TDOUBLE		((9  << SYSMEM_TYPE_SHIFT) | sizeof(double))
#define SYSMEM_TNUMBER		((10 << SYSMEM_TYPE_SHIFT) | sizeof(lua_Number))
#define SYSMEM_TBITSTRING	((11 << SYSMEM_TYPE_SHIFT) | sizeof(unsigned char))
#define SYSMEM_SIZE_MASK	0x00FF
#define SYSMEM_TYPE_MASK	0xFFFF

#define SYSMEM_UDATA		0x010000  /* memory allocated as userdata */
#define SYSMEM_ALLOC		0x020000  /* memory allocated */
#define SYSMEM_MAP		0x080000  /* memory mapped */
#define SYSMEM_ISTREAM		0x100000  /* buffer assosiated with input stream */
#define SYSMEM_OSTREAM		0x200000  /* buffer assosiated with output stream */
#define SYSMEM_ISTREAM_BUFIO	0x400000  /* input stream can operate with buffers */
#define SYSMEM_OSTREAM_BUFIO	0x800000  /* output stream can operate with buffers */
    unsigned int flags;
};

#define memisptr(mb)		(!((mb)->flags & (SYSMEM_UDATA | SYSMEM_ALLOC | SYSMEM_MAP)))
#define memtype(mb)		((mb)->flags & SYSMEM_TYPE_MASK)
#define memtypesize(mb)		((mb)->flags & SYSMEM_SIZE_MASK)
#define memlen(type, nitems)	((type) != SYSMEM_TBITSTRING				\
				 ? (nitems) * ((type) & SYSMEM_SIZE_MASK)		\
				 : ((nitems) >> 3) + 1)

static const int type_flags[] = {
    SYSMEM_TCHAR, SYSMEM_TUCHAR, SYSMEM_TSHORT, SYSMEM_TUSHORT,
    SYSMEM_TINT, SYSMEM_TUINT, SYSMEM_TLONG, SYSMEM_TULONG,
    SYSMEM_TFLOAT, SYSMEM_TDOUBLE, SYSMEM_TNUMBER, SYSMEM_TBITSTRING
};

static const char *const type_names[] = {
    "char", "uchar", "short", "ushort", "int", "uint", "long", "ulong",
    "float", "double", "number", "bitstring", NULL
};

/* MemBuffer environ. table reserved indexes */
enum {
    SYSMEM_INPUT = 1,
    SYSMEM_OUTPUT
};


static int membuf_addlstring (lua_State *L, struct membuf *mb,
                              const char *s, size_t n);


static struct membuf *
mem_tobuffer (lua_State *L, int idx)
{
    struct membuf *mb = lua_touserdata(L, idx);

    if (mb && lua_getmetatable(L, idx)) {
	int is_buffer;

	luaL_getmetatable(L, MEM_TYPENAME);
	is_buffer = lua_rawequal(L, -2, -1);
	lua_pop(L, 2);
	if (is_buffer) return mb;
    }
    return NULL;
}

/*
 * Arguments: ..., {string | membuf_udata}
 */
int
sys_buffer_read_init (lua_State *L, int idx, struct sys_buffer *sb)
{
    struct membuf *mb = mem_tobuffer(L, idx);

    if (mb) {
	sb->ptr.r = mb->data;
	sb->size = mb->offset;
	sb->mb = mb;
	return 1;
    }
    else {
	sb->ptr.r = lua_tolstring(L, idx, &sb->size);
	if (sb->ptr.r) {
	    sb->mb = NULL;
	    return 1;
	}
    }
    return 0;
}

void
sys_buffer_read_next (struct sys_buffer *sb, size_t n)
{
    struct membuf *mb = sb->mb;

    if (!mb) return;

    if (mb->offset == (int) n)
	mb->offset = 0;
    else {
	/* move tail */
	mb->offset -= n;
	memmove(mb->data, mb->data + n, mb->offset);
    }
}

/*
 * Arguments: ..., [membuf_udata]
 */
void
sys_buffer_write_init (lua_State *L, int idx, struct sys_buffer *sb,
                       char *buf, size_t buflen)
{
    struct membuf *mb = buf ? mem_tobuffer(L, idx)
     : checkudata(L, idx, MEM_TYPENAME);

    if (mb) {
	sb->ptr.w = mb->data + mb->offset;
	sb->size = mb->len - mb->offset;
	sb->mb = mb;
    }
    else {
	sb->ptr.w = buf;
	sb->size = buflen;
	sb->mb = NULL;
    }
}

int
sys_buffer_write_next (lua_State *L, struct sys_buffer *sb,
                       char *buf, size_t buflen)
{
    struct membuf *mb = sb->mb;

    if (mb) {
	if (!buflen) mb->offset = mb->len;
	if (!membuf_addlstring(L, mb, NULL, buflen))
	    return 0;
	sb->ptr.w = mb->data + mb->offset;
	sb->size = mb->len - mb->offset;
    }
    else {
	struct sys_buffer *osb = (void *) buf;
	size_t size;
	char *p;

	if (sb->ptr.w == buf) {
	    size = sb->size;
	    p = malloc(2 * size);
	    if (!p) return 0;
	    memcpy(p, buf, size);
	}
	else {
	    size = 2 * osb->size;
	    p = realloc(osb->ptr.w, 2 * size);
	    if (!p) return 0;
	    sb->size = size;
	}
	sb->ptr.w = p + size;
	osb->ptr.w = p;
	osb->size = size;
    }
    return 1;
}

int
sys_buffer_write_done (lua_State *L, struct sys_buffer *sb,
                       char *buf, size_t tail)
{
    struct membuf *mb = sb->mb;

    if (mb) {
	mb->offset += tail;
	return 0;
    }
    else {
	if (sb->ptr.w == buf)
	    lua_pushlstring(L, buf, tail);
	else {
	    struct sys_buffer *osb = (void *) buf;
	    lua_pushlstring(L, osb->ptr.w, osb->size + tail);
	    free(osb->ptr.w);
	}
	return 1;
    }
}


/*
 * Arguments: [num_bytes (number)]
 * Returns: membuf_udata
 */
static int
mem_new (lua_State *L)
{
    const size_t len = lua_tointeger(L, 1);
    struct membuf *mb = lua_newuserdata(L, sizeof(struct membuf) + len);

    memset(mb, 0, sizeof(struct membuf) + len);
    mb->flags = SYSMEM_TCHAR;
    if (len) {
	mb->data = (char *) (mb + 1);
	mb->len = len;
	mb->flags |= SYSMEM_UDATA;
    }

    luaL_getmetatable(L, MEM_TYPENAME);
    lua_setmetatable(L, -2);
    return 1;
}

/*
 * Arguments: membuf_udata, [type (string)]
 * Returns: membuf_udata | type (string)
 */
static int
mem_type (lua_State *L)
{
    struct membuf *mb = checkudata(L, 1, MEM_TYPENAME);

    if (lua_gettop(L) > 1) {
	const int type = type_flags[luaL_checkoption(L, 2, NULL, type_names)];

	mb->flags &= ~SYSMEM_TYPE_MASK;
	mb->flags |= type;
	lua_settop(L, 1);
    }
    else {
	lua_pushstring(L,
	 type_names[(mb->flags & SYSMEM_TYPE_MASK) >> SYSMEM_TYPE_SHIFT]);
    }
    return 1;
}

/*
 * Arguments: membuf_udata
 * Returns: size (number)
 */
static int
mem_typesize (lua_State *L)
{
    struct membuf *mb = checkudata(L, 1, MEM_TYPENAME);

    lua_pushinteger(L, memtypesize(mb));
    return 1;
}

/*
 * Arguments: membuf_udata, [num_bytes (number), zerofill (boolean)]
 * Returns: [membuf_udata]
 */
static int
mem_alloc (lua_State *L)
{
    struct membuf *mb = checkudata(L, 1, MEM_TYPENAME);
    const int len = luaL_optinteger(L, 2, BUFF_INITIALSIZE);
    const int zerofill = lua_isboolean(L, -1) && lua_toboolean(L, -1);

    mb->flags |= SYSMEM_ALLOC;
    mb->len = len;
    mb->offset = 0;
    mb->data = zerofill ? calloc(len, 1) : malloc(len);
    lua_settop(L, 1);
    return mb->data ? 1 : 0;
}

/*
 * Arguments: membuf_udata, num_bytes (number)
 * Returns: [membuf_udata]
 */
static int
mem_realloc (lua_State *L)
{
    struct membuf *mb = checkudata(L, 1, MEM_TYPENAME);
    const int len = luaL_checkinteger(L, 2);
    void *p = realloc(mb->data, len);

    if (!p) return 0;
    mb->data = p;
    mb->len = len;
    lua_settop(L, 1);
    return 1;
}


#ifdef SYSMEM_HAVE_MMAP

/*
 * Arguments: membuf_udata, fd_udata, [protection (string: "rw"),
 *	offset (number), num_bytes (number), private/shared (boolean)]
 * Returns: [membuf_udata]
 */
static int
mem_map (lua_State *L)
{
    struct membuf *mb = checkudata(L, 1, MEM_TYPENAME);
    const fd_t *fdp = checkudata(L, 2, FD_TYPENAME);
    fd_t fd = fdp ? (fd_t) *fdp : (fd_t) -1;  /* named or anonymous mapping */
    const char *protstr = lua_tostring(L, 3);
    const lua_Number offset = lua_tonumber(L, 4);
    const int64_t off = (int64_t) offset;  /* to avoid warning */
    size_t len = (size_t) lua_tointeger(L, 5);
    const int is_private = lua_isboolean(L, -1) && lua_toboolean(L, -1);
    int prot = 0, flags;
    void *ptr;

#ifndef _WIN32
#ifndef MAP_ANON
    int is_anon = 0;
#endif

    sys_vm_leave();
    /* length */
    if (!len) {
	struct stat sb;
	if (fd == -1 || fstat(fd, &sb) == -1)
	    goto err;
	sb.st_size -= off;
	len = (sb.st_size < (off_t) ~((size_t) 0))
	 ? (size_t) sb.st_size : ~((size_t) 0);
    }
    /* protection and flags */
    prot = PROT_READ;
    if (protstr) {
	if (protstr[0] == 'w')
	    prot = PROT_WRITE;
	else if (protstr[1] == 'w')
	    prot |= PROT_WRITE;
    }
    flags = is_private ? MAP_PRIVATE : MAP_SHARED;
    /* anonymous shared memory? */
    if (fd == -1) {
#ifdef MAP_ANON
	flags |= MAP_ANON;
#else
	if ((fd = open("/dev/zero", O_RDWR)) == -1)
	    goto err;
	is_anon = 1;
#endif
    }
    /* map file to memory */
    ptr = mmap(0, len, prot, flags, (int) fd, off);
#ifndef MAP_ANON
    if (is_anon) close(fd);
#endif
    if (ptr == MAP_FAILED) goto err;

#else
    sys_vm_leave();
    /* length */
    if (!len) {
	DWORD size_hi, size_lo;
	int64_t size;

	if (fd == (fd_t) -1) goto err;
	size_lo = GetFileSize(fd, &size_hi);
	if (size_lo == -1L && SYS_ERRNO != NO_ERROR)
	    goto err;
	size = INT64_MAKE(size_lo, size_hi) - off;
	len = (size < (int64_t) ~((DWORD) 0))
	 ? (DWORD) size : ~((DWORD) 0);
    }
    /* protection and flags */
    if (protstr && (protstr[0] == 'w' || protstr[1] == 'w')) {
	prot = PAGE_READWRITE;
	flags = FILE_MAP_WRITE;
    } else {
	prot = PAGE_READONLY;
	flags = FILE_MAP_READ;
    }
    if (is_private) {
	prot = PAGE_WRITECOPY;
	flags = FILE_MAP_COPY;
    }
    /* map file to memory */
    {
	const DWORD off_hi = INT64_HIGH(off);
	const DWORD off_lo = INT64_LOW(off);
	HANDLE hmap = CreateFileMapping(fd, NULL, prot, 0, len, NULL);

	if (!hmap) goto err;
	ptr = MapViewOfFile(hmap, flags, off_hi, off_lo, len);
	CloseHandle(hmap);
	if (!ptr) goto err;
    }
#endif /* !Win32 */
    sys_vm_enter();

    mb->flags |= SYSMEM_MAP;
    mb->len = len;
    mb->data = ptr;
    lua_settop(L, 1);
    return 1;
 err:
    sys_vm_enter();
    return sys_seterror(L, 0);
}

/*
 * Arguments: membuf_udata
 * Returns: [membuf_udata]
 */
static int
mem_sync (lua_State *L)
{
    struct membuf *mb = checkudata(L, 1, MEM_TYPENAME);
    int res;

    sys_vm_leave();
#ifndef _WIN32
    res = msync(mb->data, mb->len, MS_SYNC);
#else
    res = !FlushViewOfFile(mb->data, 0);
#endif
    sys_vm_enter();

    return !res ? 1 : 0;
}

#endif /* SYSMEM_HAVE_MMAP */


/*
 * Arguments: membuf_udata
 * Returns: membuf_udata
 */
static int
mem_free (lua_State *L)
{
    struct membuf *mb = checkudata(L, 1, MEM_TYPENAME);

    if (mb->data) {
	const unsigned int mb_flags = mb->flags;

	switch (mb_flags & (SYSMEM_ALLOC | SYSMEM_MAP)) {
	case SYSMEM_ALLOC:
	    free(mb->data);
	    break;
#ifdef SYSMEM_HAVE_MMAP
	case SYSMEM_MAP:
#ifndef _WIN32
	    munmap(mb->data, mb->len);
#else
	    UnmapViewOfFile(mb->data);
#endif
	    break;
#endif /* SYSMEM_HAVE_MMAP */
	}
	mb->data = NULL;
	mb->flags &= SYSMEM_TYPE_MASK;
    }
    return 1;
}

/*
 * Arguments: membuf_udata, source (membuf_udata), num_bytes (number)
 * Returns: [membuf_udata]
 */
static int
mem_memcpy (lua_State *L)
{
    struct membuf *mb = checkudata(L, 1, MEM_TYPENAME);
    struct membuf *src = checkudata(L, 2, MEM_TYPENAME);
    const int len = luaL_checkinteger(L, 3);

    lua_settop(L, 1);
    return memcpy(mb->data, src->data, len) ? 1 : 0;
}

/*
 * Arguments: membuf_udata, byte (number), num_bytes (number)
 * Returns: [membuf_udata]
 */
static int
mem_memset (lua_State *L)
{
    struct membuf *mb = checkudata(L, 1, MEM_TYPENAME);
    const int ch = lua_tointeger(L, 2);
    const int len = luaL_checkinteger(L, 3);

    lua_settop(L, 1);
    return memset(mb->data, ch, len) ? 1 : 0;
}

/*
 * Arguments: membuf_udata, [num_bytes (number)]
 * Returns: membuf_udata | num_bytes (number)
 */
static int
mem_length (lua_State *L)
{
    struct membuf *mb = checkudata(L, 1, MEM_TYPENAME);

    if (lua_gettop(L) == 1)
	lua_pushinteger(L, mb->len);
    else {
	if (mb->flags & SYSMEM_MAP)
	    luaL_argerror(L, 1, "membuf is mapped");

	mb->len = lua_tointeger(L, 2);
	lua_settop(L, 1);
    }
    return 1;
}

/*
 * Arguments: membuf_udata, [offset (number)]
 * Returns: pointer (ludata)
 */
static int
mem_getptr (lua_State *L)
{
    struct membuf *mb = checkudata(L, 1, MEM_TYPENAME);
    const int off = lua_tointeger(L, 2);
    void *ptr = mb->data + memtypesize(mb) * off;

    lua_pushlightuserdata(L, ptr);
    return 1;
}

/*
 * Arguments: membuf_udata, pointer (ludata), [offset (number)]
 * Returns: membuf_udata
 */
static int
mem_setptr (lua_State *L)
{
    struct membuf *mb = checkudata(L, 1, MEM_TYPENAME);
    char *ptr = lua_touserdata(L, 2);
    const int off = lua_tointeger(L, 3);

    if (memisptr(mb)) {
	mb->data = ptr;
	mb->offset = off;
	lua_settop(L, 1);
	return 1;
    }
    return 0;
}

/*
 * Arguments: membuf_udata, [offset (number), target (membuf_udata)]
 * Returns: membuf_udata | target (membuf_udata)
 */
static int
mem_call (lua_State *L)
{
    struct membuf *mb = checkudata(L, 1, MEM_TYPENAME);
    const int off = lua_tointeger(L, 2);
    void *ptr = mb->data + memtypesize(mb) * off;

    if (lua_gettop(L) < 3)
	lua_settop(L, 1);
    else {
	mb = checkudata(L, 3, MEM_TYPENAME);
	lua_settop(L, 3);
    }
    if (memisptr(mb)) {
	mb->data = ptr;
	return 1;
    }
    return 0;
}

/*
 * Arguments: membuf_udata, offset (number)
 * Returns: value
 */
static int
mem_index (lua_State *L)
{
    if (lua_type(L, 2) == LUA_TNUMBER) {
	struct membuf *mb = checkudata(L, 1, MEM_TYPENAME);
	const int off = lua_tointeger(L, 2);
	const int type = memtype(mb);
	char *ptr = mb->data + memlen(type, off);

	switch (type) {
	case SYSMEM_TCHAR: lua_pushnumber(L, *((char *) ptr)); break;
	case SYSMEM_TUCHAR: lua_pushnumber(L, *((unsigned char *) ptr)); break;
	case SYSMEM_TSHORT: lua_pushnumber(L, *((short *) ptr)); break;
	case SYSMEM_TUSHORT: lua_pushnumber(L, *((unsigned short *) ptr)); break;
	case SYSMEM_TINT: lua_pushinteger(L, *((int *) ptr)); break;
	case SYSMEM_TUINT: lua_pushinteger(L, *((unsigned int *) ptr)); break;
	case SYSMEM_TLONG: lua_pushnumber(L, *((long *) ptr)); break;
	case SYSMEM_TULONG: lua_pushnumber(L, *((unsigned long *) ptr)); break;
	case SYSMEM_TFLOAT: lua_pushnumber(L, *((float *) ptr)); break;
	case SYSMEM_TDOUBLE: lua_pushnumber(L, *((double *) ptr)); break;
	case SYSMEM_TNUMBER: lua_pushnumber(L, *((lua_Number *) ptr)); break;
	case SYSMEM_TBITSTRING: lua_pushboolean(L, *(ptr - 1) & (1 << (off & 7))); break;
	}
    } else {
	lua_getmetatable(L, 1);
	lua_replace(L, 1);
	lua_rawget(L, 1);
    }
    return 1;
}

/*
 * Arguments: membuf_udata, offset (number), value
 */
static int
mem_newindex (lua_State *L)
{
    struct membuf *mb = checkudata(L, 1, MEM_TYPENAME);
    const int off = lua_tointeger(L, 2);
    const int type = memtype(mb);
    char *ptr = mb->data + memlen(type, off);

    switch (lua_type(L, 3)) {
    case LUA_TNUMBER: {
	    lua_Number num = lua_tonumber(L, 3);

	    switch (type) {
	    case SYSMEM_TCHAR: *((char *) ptr) = (char) num; break;
	    case SYSMEM_TUCHAR: *((unsigned char *) ptr) = (unsigned char) num; break;
	    case SYSMEM_TSHORT: *((short *) ptr) = (short) num; break;
	    case SYSMEM_TUSHORT: *((unsigned short *) ptr) = (unsigned short) num; break;
	    case SYSMEM_TINT: *((int *) ptr) = (int) num; break;
	    case SYSMEM_TUINT: *((unsigned int *) ptr) = (unsigned int) num; break;
	    case SYSMEM_TLONG: *((long *) ptr) = (long) num; break;
	    case SYSMEM_TULONG: *((unsigned long *) ptr) = (unsigned long) num; break;
	    case SYSMEM_TFLOAT: *((float *) ptr) = (float) num; break;
	    case SYSMEM_TDOUBLE: *((double *) ptr) = (double) num; break;
	    case SYSMEM_TNUMBER: *((lua_Number *) ptr) = num; break;
	    case SYSMEM_TBITSTRING: luaL_typeerror(L, 3, "boolean"); break;
	    }
	}
	break;
    case LUA_TBOOLEAN:
	if (type != SYSMEM_TBITSTRING)
	    luaL_typeerror(L, 1, "bitstring");
	else {
	    const int bit = 1 << (off & 7);

	    --ptr;  /* correct address */
	    if (lua_toboolean(L, 3)) *ptr |= bit;  /* set */
	    else *ptr &= ~bit;  /* clear */
	}
	break;
    case LUA_TSTRING:
	{
	    size_t len;
	    const char *s = lua_tolstring(L, 3, &len);

	    memcpy(ptr, s, len);
	}
	break;
    default:
	luaL_typeerror(L, 3, "membuf value");
    }
    return 0;
}

/*
 * Arguments: membuf_udata
 * Returns: string
 */
static int
mem_tostring (lua_State *L)
{
    struct membuf *mb = checkudata(L, 1, MEM_TYPENAME);

    if (mb->data)
	lua_pushfstring(L, MEM_TYPENAME " (%p)", mb->data);
    else
	lua_pushliteral(L, MEM_TYPENAME " (closed)");
    return 1;
}


#include "membuf.c"


static luaL_reg mem_meth[] = {
    {"type",		mem_type},
    {"typesize",	mem_typesize},
    {"alloc",		mem_alloc},
    {"realloc",		mem_realloc},
#ifdef SYSMEM_HAVE_MMAP
    {"map",		mem_map},
    {"sync",		mem_sync},
#endif
    {"free",		mem_free},
    {"__gc",		mem_free},
    {"memcpy",		mem_memcpy},
    {"memset",		mem_memset},
    {"length",		mem_length},
    {"__len",		mem_length},
    {"getptr",		mem_getptr},
    {"setptr",		mem_setptr},
    {"__call",		mem_call},
    {"__index",		mem_index},
    {"__newindex",	mem_newindex},
    {"__tostring",	mem_tostring},
    /* stream operations */
    {"write",		membuf_write},
    {"writeln",		membuf_writeln},
    {"tostring",	membuf_tostring},
    {"seek",		membuf_seek},
    {"output",		membuf_output},
    {"input",		membuf_input},
    {"read",		membuf_read},
    {"flush",		membuf_flush},
    {"close",		membuf_close},
    {SYS_BUFIO_TAG,	NULL},  /* can operate with buffers */
    {NULL, NULL}
};

static luaL_reg mem_lib[] = {
    {"pointer",		mem_new},
    {NULL, NULL}
};


static void
luaopen_sys_mem (lua_State *L)
{
    luaL_newmetatable(L, MEM_TYPENAME);
    luaL_register(L, NULL, mem_meth);
    luaL_register(L, "sys.mem", mem_lib);
    lua_pop(L, 2);
}

