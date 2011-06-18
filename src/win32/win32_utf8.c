/* Lua System: Win32 specifics: UTF-8 */

/*
 * Convert UTF-8 to microsoft unicode.
 */
static void *
utf8_to_utf16 (const char *s)
{
    WCHAR *ws;
    int n;

    n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    ws = malloc(n * sizeof(WCHAR));
    if (!ws) return NULL;

    n = MultiByteToWideChar(CP_UTF8, 0, s, -1, ws, n);
    if (!n) {
	free(ws);
	ws = NULL;
    }
    return ws;
}

/*
 * Convert microsoft unicode to UTF-8.
 */
static void *
utf16_to_utf8 (const WCHAR *ws)
{
    char *s;
    int n;

    n = WideCharToMultiByte(CP_UTF8, 0, ws, -1, NULL, 0, NULL, 0);
    s = malloc(n);
    if (!s) return NULL;

    n = WideCharToMultiByte(CP_UTF8, 0, ws, -1, s, n, NULL, 0);
    if (!n) {
	free(s);
	s = NULL;
    }
    return s;
}

/*
 * Convert an ansi string to microsoft unicode, based on the
 * current codepage settings for file apis.
 */
static void *
mbcs_to_utf16 (const char *mbcs)
{
    WCHAR *ws;
    const int codepage = AreFileApisANSI() ? CP_ACP : CP_OEMCP;
    int n;

    n = MultiByteToWideChar(codepage, 0, mbcs, -1, NULL, 0);
    ws = malloc(n * sizeof(WCHAR));
    if (!ws) return NULL;

    n = MultiByteToWideChar(codepage, 0, mbcs, -1, ws, n);
    if (!n) {
	free(ws);
	ws = NULL;
    }
    return ws;
}

/*
 * Convert microsoft unicode to multibyte character string, based on the
 * user's Ansi codepage.
 */
static void *
utf16_to_mbcs (const WCHAR *ws)
{
    char *mbcs;
    const int codepage = AreFileApisANSI() ? CP_ACP : CP_OEMCP;
    int n;

    n = WideCharToMultiByte(codepage, 0, ws, -1, NULL, 0, NULL, 0);
    mbcs = malloc(n);
    if (!mbcs) return NULL;

    n = WideCharToMultiByte(codepage, 0, ws, -1, mbcs, n, NULL, 0);
    if (!n) {
	free(mbcs);
	mbcs = NULL;
    }
    return mbcs;
}

/*
 * Convert multibyte character string to UTF-8.
 */
static void *
mbcs_to_utf8 (const char *mbcs)
{
    char *s;
    WCHAR *ws;

    ws = mbcs_to_utf16(mbcs);
    if (!ws) return NULL;

    s = utf16_to_utf8(ws);
    free(ws);
    return s;
}

/*
 * Convert UTF-8 to multibyte character string.
 */
static void *
utf8_to_mbcs (const char *s)
{
    char *mbcs;
    WCHAR *ws;

    ws = utf8_to_utf16(s);
    if (!ws) return NULL;

    mbcs = utf16_to_mbcs(ws);
    free(ws);
    return mbcs;
}

/*
 * Convert UTF-8 to OS filename.
 */
void *
utf8_to_filename (const char *s)
{
    return is_WinNT ? utf8_to_utf16(s) : utf8_to_mbcs(s);
}

/*
 * Convert OS filename to UTF-8.
 */
char *
filename_to_utf8 (const void *s)
{
    return is_WinNT ? utf16_to_utf8(s) : mbcs_to_utf8(s);
}

/*
 * Arguments: UTF-8 string
 * Returns: multibyte character string
 */
static int
utf8_console (lua_State *L)
{
    const char *s = luaL_checkstring(L, 1);
    void *mbcs = utf8_to_mbcs(s);

    if (!mbcs)
	return sys_seterror(L, ERROR_NOT_ENOUGH_MEMORY);

    lua_pushstring(L, mbcs);
    free(mbcs);
    return 1;
}

