/* Lua System: Memory Buffers: Streams */

#define MEM_BUFLINE	256


/*
 * Arguments: membuf_udata, ...
 */
static int
stream_write (lua_State *L, struct membuf *mb)
{
  const int bufio = (mb->flags & MEM_OSTREAM_BUFIO);
  int res;

  lua_getfenv(L, 1);
  lua_rawgeti(L, -1, MEM_OUTPUT);  /* stream object */
  lua_getfield(L, -1, "write");
  lua_insert(L, -2);

  if (bufio)
    lua_pushvalue(L, 1);
  else
    lua_pushlstring(L, mb->data, mb->offset);
  lua_call(L, 2, 1);

  res = lua_toboolean(L, -1);
  lua_pop(L, 2);  /* pop environ. and result */

  if (res && !bufio) mb->offset = 0;
  return res;
}

static int
membuf_addlstring (lua_State *L, struct membuf *mb, const char *s,
                   const size_t size)
{
  int offset = mb->offset;
  size_t len = mb->len;
  const size_t newlen = offset + size;

  if (newlen >= len) {
    const unsigned int flags = mb->flags;
    void *p;

    if ((flags & MEM_OSTREAM) && stream_write(L, mb)) {
      offset = mb->offset;
      len = mb->len;
      if (size < len - offset)
        goto end;
    }
    while ((len *= 2) <= newlen)
      continue;
    if (!(flags & MEM_ALLOC) || !(p = realloc(mb->data, len)))
      return 0;
    mb->len = len;
    mb->data = p;
  }
 end:
  if (s != NULL) {
    memcpy(mb->data + offset, s, size);
    mb->offset = offset + size;
  }
  return 1;
}

/*
 * Arguments: membuf_udata, string ...
 * Returns: [boolean]
 */
static int
membuf_write (lua_State *L)
{
  struct membuf *mb = checkudata(L, 1, MEM_TYPENAME);
  int narg, i;

  narg = lua_gettop(L);
  for (i = 2; i <= narg; ++i) {
    size_t len = lua_rawlen(L, i);
    if (len && !membuf_addlstring(L, mb, lua_tostring(L, i), len))
      return 0;
  }
  lua_pushboolean(L, 1);
  return 1;
}

/*
 * Arguments: membuf_udata, string ...
 * Returns: [boolean]
 */
static int
membuf_writeln (lua_State *L)
{
  lua_pushliteral(L, "\n");
  return membuf_write(L);
}

/*
 * Arguments: membuf_udata, [num_bytes (number), offset (number)]
 * Returns: string
 */
static int
membuf_tostring (lua_State *L)
{
  struct membuf *mb = checkudata(L, 1, MEM_TYPENAME);
  const int len = luaL_optinteger(L, 2, mb->offset);
  const int off = lua_tointeger(L, 3);

  lua_pushlstring(L, mb->data + off, len);
  return 1;
}

/*
 * Arguments: membuf_udata, [offset (number)]
 * Returns: membuf_udata | offset (number)
 */
static int
membuf_seek (lua_State *L)
{
  struct membuf *mb = checkudata(L, 1, MEM_TYPENAME);

  if (lua_gettop(L) > 1) {
    mb->offset = lua_tointeger(L, 2);
    lua_settop(L, 1);
  } else
    lua_pushinteger(L, mb->offset);
  return 1;
}


/*
 * Arguments: membuf_udata, stream
 */
static int
membuf_assosiate (lua_State *L, int type)
{
  struct membuf *mb = checkudata(L, 1, MEM_TYPENAME);
  const int idx = (type == MEM_ISTREAM) ? MEM_INPUT : MEM_OUTPUT;

  lua_settop(L, 2);
  if (lua_isnoneornil(L, 2))
    mb->flags &= ~type;
  else {
    mb->flags |= type;

    lua_getfield(L, -1, SYS_BUFIO_TAG);
    if (!lua_isnil(L, -1)) {
      mb->flags |= (type == MEM_ISTREAM)
       ? MEM_ISTREAM_BUFIO : MEM_OSTREAM_BUFIO;
    }
    lua_pop(L, 1);
  }

  lua_getfenv(L, 1);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setfenv(L, 1);
  }
  lua_pushvalue(L, 2);
  lua_rawseti(L, -2, idx);
  return 0;
}

/*
 * Arguments: membuf_udata, consumer_stream
 */
static int
membuf_output (lua_State *L)
{
  return membuf_assosiate(L, MEM_OSTREAM);
}

/*
 * Arguments: membuf_udata, producer_stream
 */
static int
membuf_input (lua_State *L)
{
  return membuf_assosiate(L, MEM_ISTREAM);
}


/*
 * Arguments: membuf_udata, ..., stream, function
 * Returns: [boolean]
 */
static void
stream_read (lua_State *L, size_t l, const int bufio)
{
  int narg = 1;

  lua_pushvalue(L, -2);
  lua_pushvalue(L, -2);

  if (bufio) {
    lua_pushvalue(L, 1);
    ++narg;
  }
  if (l != (size_t) -1) {
    lua_pushinteger(L, l);
    ++narg;
  }
  lua_call(L, narg, 1);
}

static int
read_bytes (lua_State *L, struct membuf *mb, size_t l)
{
  int n = mb->offset;

  if (!n && (mb->flags & MEM_ISTREAM)) {
    stream_read(L, l, (mb->flags & MEM_ISTREAM_BUFIO));
    return 1;
  }

  if (l > (size_t) n) l = n;
  if (l) {
    char *p = mb->data;  /* avoid warning */
    lua_pushlstring(L, p, l);
    n -= l;
    mb->offset = n;
    if (n) memmove(p, p + l, n);
  } else
    lua_pushnil(L);
  return 1;
}

static int
read_line (lua_State *L, struct membuf *mb)
{
  const char *nl, *s = mb->data;
  size_t l, n = mb->offset;

  if (n && (nl = memchr(s, '\n', n))) {
    char *p = mb->data;  /* avoid warning */
    l = nl - p;
    lua_pushlstring(L, p, l);
    n -= l + 1;
    mb->offset = n;
    if (n) memmove(p, nl + 1, n);
    return 1;
  }
  if (!(mb->flags & MEM_ISTREAM)) {
    n = 1;
    goto end;
  }
  for (; ; ) {
    stream_read(L, MEM_BUFLINE, 0);
    s = lua_tolstring(L, -1, &n);
    if (!n) {
      n = 1;
      break;
    }
    if (*s == '\n')
      break;
    nl = memchr(s + 1, '\n', n - 1);
    l = !nl ? n : (size_t) (nl - s);
    if (!membuf_addlstring(L, mb, s, l))
      return 0;
    /* tail */
    if (nl) {
      n -= l;
      s = nl;
      break;
    }
    lua_pop(L, 1);
  }
 end:
  l = mb->offset;
  if (l != 0)
    lua_pushlstring(L, mb->data, l);
  else
    lua_pushnil(L);
  mb->offset = 0;
  return (!--n) ? 1 : membuf_addlstring(L, mb, s + 1, n);
}

/*
 * Arguments: membuf_udata, [count (number) | mode (string: "*l", "*a")]
 * Returns: [string | number]
 */
static int
membuf_read (lua_State *L)
{
  struct membuf *mb = checkudata(L, 1, MEM_TYPENAME);

  lua_settop(L, 2);
  if (mb->flags & MEM_ISTREAM) {
    lua_getfenv(L, 1);
    lua_rawgeti(L, -1, MEM_INPUT);  /* stream object */
    lua_getfield(L, -1, "read");
    lua_insert(L, -2);
  }

  if (lua_type(L, 2) == LUA_TNUMBER)
    read_bytes(L, mb, lua_tointeger(L, 2));
  else {
    const char *s = luaL_optstring(L, 2, "*a");

    switch (s[1]) {
    case 'l':
      return read_line(L, mb);
    case 'a':
      read_bytes(L, mb, ~((size_t) 0));
      break;
    default:
      luaL_argerror(L, 2, "invalid option");
    }
  }
  return 1;
}

/*
 * Arguments: membuf_udata, [close (boolean)]
 * Returns: [membuf_udata]
 */
static int
membuf_flush (lua_State *L)
{
  struct membuf *mb = checkudata(L, 1, MEM_TYPENAME);
  const int is_close = lua_toboolean(L, 2);
  int res = 1;

  if (mb->flags & MEM_OSTREAM) {
    res = stream_write(L, mb);
    if (is_close) mem_free(L);
  }
  lua_settop(L, 1);
  return res;
}

/*
 * Arguments: membuf_udata
 * Returns: [membuf_udata]
 */
static int
membuf_close (lua_State *L)
{
  lua_pushboolean(L, 1);
  return membuf_flush(L);
}

