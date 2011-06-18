/* Lua System: File System */

#ifndef _WIN32
#include <sys/statvfs.h>
#include <dirent.h>
#endif

#define DIR_TYPENAME	"sys.dir"

/* Directory iterator */
struct dir {
#ifndef _WIN32
    DIR *data;
#else
    int is_root;  /* list logical drives? */
    HANDLE h;
    WIN32_FIND_DATAW data;
#endif
};


/*
 * Arguments: path (string), [more_info (boolean)]
 * Returns: [is_directory (boolean), is_file (boolean),
 *	is_read (boolean), is_write (boolean), is_execute (boolean),
 *	[is_link (boolean), size (number),
 *	access_time (number), modify_time (number), create_time (number)]]
 */
static int
sys_stat (lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    const int more_info = lua_toboolean(L, 2);
    struct stat st;
    int res;

    sys_vm_leave();
    res = stat(path, &st);
    sys_vm_enter();

    if (!res) {
	/* is directory? */
	lua_pushboolean(L,
#ifndef _WIN32
	 S_ISDIR(st.st_mode)
#else
	 st.st_mode & _S_IFDIR
#endif
	);
	/* is regular file? */
	lua_pushboolean(L,
#ifndef _WIN32
	 S_ISREG(st.st_mode)
#else
	 st.st_mode & _S_IFREG
#endif
	);
	/* can anyone read from file? */
	lua_pushboolean(L,
#ifndef _WIN32
	 st.st_mode & (S_IRUSR | S_IRGRP | S_IROTH)
#else
	 st.st_mode & _S_IREAD
#endif
	);
	/* can anyone write to file? */
	lua_pushboolean(L,
#ifndef _WIN32
	 st.st_mode & (S_IWUSR | S_IWGRP | S_IWOTH)
#else
	 st.st_mode & _S_IWRITE
#endif
	);
	/* can anyone execute the file? */
	lua_pushboolean(L,
#ifndef _WIN32
	 st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)
#else
	 st.st_mode & _S_IEXEC
#endif
	);
	if (more_info) {
	    /* is link? */
#ifndef _WIN32
	    lua_pushboolean(L, S_ISLNK(st.st_mode));
#else
	    DWORD attr;
	    {
		void *os_path = utf8_to_filename(path);
		if (!os_path)
		    return sys_seterror(L, ERROR_NOT_ENOUGH_MEMORY);

		attr = is_WinNT
		 ? GetFileAttributesW(os_path)
		 : GetFileAttributesA(os_path);

		free(os_path);
	    }
	    lua_pushboolean(L, attr > 0 && (attr & FILE_ATTRIBUTE_REPARSE_POINT));
#endif
	    lua_pushnumber(L, st.st_size);  /* size in bytes */
	    lua_pushnumber(L, st.st_atime);  /* access time */
	    lua_pushnumber(L, st.st_mtime);  /* modification time */
	    lua_pushnumber(L, st.st_ctime);  /* creation time */
	    return 10;
	}
	return 5;
    }
    return sys_seterror(L, 0);
}

/*
 * Arguments: path (string)
 * Returns: [total_bytes (number), available_bytes (number), free_bytes (number)]
 */
static int
sys_statfs (lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    int64_t ntotal, navail, nfree;
    int res;

#ifndef _WIN32
    struct statvfs buf;

    res = statvfs(path, &buf);

    ntotal = buf.f_blocks * buf.f_frsize;
    nfree = buf.f_bfree * buf.f_bsize;
    navail = buf.f_bavail * buf.f_bsize;
#else
    ULARGE_INTEGER na, nt, nf;

    SetErrorMode(SEM_FAILCRITICALERRORS);  /* for floppy disks */
    {
	void *os_path = utf8_to_filename(path);
	if (!os_path)
	    return sys_seterror(L, ERROR_NOT_ENOUGH_MEMORY);

	res = is_WinNT
	 ? !GetDiskFreeSpaceExW(os_path, &na, &nt, &nf)
	 : !GetDiskFreeSpaceExA(os_path, &na, &nt, &nf);

	free(os_path);
    }

    ntotal = (int64_t) nt.QuadPart;
    nfree = (int64_t) nf.QuadPart;
    navail = (int64_t) na.QuadPart;
#endif
    if (!res) {
	lua_pushnumber(L, (lua_Number) ntotal);
	lua_pushnumber(L, (lua_Number) navail);
	lua_pushnumber(L, (lua_Number) nfree);
	return 3;
    }
    return sys_seterror(L, 0);
}

/*
 * Arguments: path (string)
 * Returns: [boolean]
 */
static int
sys_remove (lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    int res;

#ifndef _WIN32
    sys_vm_leave();
    res = remove(path);
    sys_vm_enter();
#else
    {
	void *os_path = utf8_to_filename(path);
	if (!os_path)
	    return sys_seterror(L, ERROR_NOT_ENOUGH_MEMORY);

	sys_vm_leave();
	res = is_WinNT
	 ? !DeleteFileW(os_path)
	 : !DeleteFileA(os_path);
	sys_vm_enter();

	free(os_path);
    }
#endif

    if (!res) {
	lua_pushboolean(L, 1);
	return 1;
    }
    return sys_seterror(L, 0);
}

/*
 * Arguments: existing_path (string), new_path (string)
 * Returns: [boolean]
 */
static int
sys_rename (lua_State *L)
{
    const char *old = luaL_checkstring(L, 1);
    const char *new = luaL_checkstring(L, 2);
    int res;

#ifndef _WIN32
    sys_vm_leave();
    res = rename(old, new);
    sys_vm_enter();
#else
    {
	void *os_old = utf8_to_filename(old);
	void *os_new = utf8_to_filename(new);
	if (!os_old || !os_new) {
	    free(os_old);
	    free(os_new);
	    return sys_seterror(L, ERROR_NOT_ENOUGH_MEMORY);
	}

	sys_vm_leave();
	res = is_WinNT
	 ? !MoveFileW(os_old, os_new)
	 : !MoveFileA(os_old, os_new);
	sys_vm_enter();

	free(os_old);
	free(os_new);
    }
#endif

    if (!res) {
	lua_pushboolean(L, 1);
	return 1;
    }
    return sys_seterror(L, 0);
}

/*
 * Arguments: path (string)
 * Returns: [string]
 */
static int
sys_realpath (lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);

#ifndef _WIN32
    char real[PATH_MAX];

    if (realpath(path, real)) {
	lua_pushstring(L, real);
	return 1;
    }
#else
    void *os_path = utf8_to_filename(path);
    if (!os_path)
	return sys_seterror(L, ERROR_NOT_ENOUGH_MEMORY);

    {
	WCHAR os_real[MAX_PATHNAME];
	const int n = is_WinNT
	 ? GetFullPathNameW(os_path, MAX_PATHNAME, os_real, NULL)
	 : GetFullPathNameA(os_path, MAX_PATHNAME, (char *) os_real, NULL);

	free(os_path);

	if (n != 0 && n < MAX_PATHNAME) {
	    void *real = filename_to_utf8(os_real);
	    if (!real)
		return sys_seterror(L, ERROR_NOT_ENOUGH_MEMORY);

	    lua_pushstring(L, real);
	    free(real);
	    return 1;
	}
    }
#endif
    return sys_seterror(L, 0);
}

/*
 * Arguments: [path (string)]
 * Returns: [boolean | pathname (string)]
 */
static int
sys_curdir (lua_State *L)
{
    const char *path = lua_tostring(L, 1);

    if (path) {
	int res;
#ifndef _WIN32
	res = chdir(path);
#else
	{
	    void *os_path = utf8_to_filename(path);
	    if (!os_path)
		return sys_seterror(L, ERROR_NOT_ENOUGH_MEMORY);

	    res = is_WinNT
	     ? !SetCurrentDirectoryW(os_path)
	     : !SetCurrentDirectoryA(os_path);

	    free(os_path);
	}
#endif
	if (!res) {
	    lua_pushboolean(L, 1);
	    return 1;
	}
    } else {
#ifndef _WIN32
	char dir[MAX_PATHNAME];

	if (getcwd(dir, MAX_PATHNAME)) {
	    lua_pushstring(L, dir);
	    return 1;
	}
#else
	WCHAR os_dir[MAX_PATHNAME];
	const int n = is_WinNT
	 ? GetCurrentDirectoryW(MAX_PATHNAME, os_dir)
	 : GetCurrentDirectoryA(MAX_PATHNAME, (char *) os_dir);

	if (n != 0 && n < MAX_PATHNAME) {
	    void *dir = filename_to_utf8(os_dir);
	    if (!dir)
		return sys_seterror(L, ERROR_NOT_ENOUGH_MEMORY);

	    lua_pushstring(L, dir);
	    free(dir);
	    return 1;
	}
#endif
    }
    return sys_seterror(L, 0);
}

/*
 * Arguments: path (string), [permissions (number)]
 * Returns: [boolean]
 */
static int
sys_mkdir (lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    int res;

#ifndef _WIN32
    mode_t perm = (mode_t) lua_tointeger(L, 2);

    res = mkdir(path, perm);
#else
    {
	void *os_path = utf8_to_filename(path);
	if (!os_path)
	    return sys_seterror(L, ERROR_NOT_ENOUGH_MEMORY);

	res = is_WinNT
	 ? !CreateDirectoryW(os_path, NULL)
	 : !CreateDirectoryA(os_path, NULL);

	free(os_path);
    }
#endif
    if (!res) {
	lua_pushboolean(L, 1);
	return 1;
    }
    return sys_seterror(L, 0);
}

/*
 * Arguments: path (string)
 * Returns: [boolean]
 */
static int
sys_rmdir (lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    int res;

#ifndef _WIN32
    res = rmdir(path);
#else
    {
	void *os_path = utf8_to_filename(path);
	if (!os_path)
	    return sys_seterror(L, ERROR_NOT_ENOUGH_MEMORY);

	res = is_WinNT
	 ? !RemoveDirectoryW(os_path)
	 : !RemoveDirectoryA(os_path);

	free(os_path);
    }
#endif
    if (!res) {
	lua_pushboolean(L, 1);
	return 1;
    }
    return sys_seterror(L, 0);
}


/*
 * Arguments: ..., directory (string)
 */
static int
sys_dir_open (lua_State *L, const int idx, struct dir *dp)
{
    const char *dir = luaL_checkstring(L, idx);

#ifndef _WIN32
    if (dp->data)
	closedir(dp->data);
    dp->data = opendir(*dir == '\0' ? "/" : dir);
    if (dp->data) return 1;
#else
    char *filename = (char *) dp->data.cFileName;

    if (*dir == '\0' || (*dir == '/' && dir[1] == '\0')) {
	/* list drive letters */
	*filename++ = 'A' - 1;
	*filename++ = ':';
	*filename++ = '\\';
	*filename = '\0';

	dp->is_root = 1;
	return 1;
    } else {
	const int len = lua_rawlen(L, idx);

	/* build search path */
	if (len >= MAX_PATH - 2)  /* concat "\\*" */
	    return 0;
	memcpy(filename, dir, len);
	filename += len - 1;
	if (*filename != '\\' || *filename != '/')
	    *(++filename) = '\\';
	*(++filename) = '*';
	*(++filename) = '\0';

	if (dp->h != INVALID_HANDLE_VALUE)
	    FindClose(dp->h);

	{
	    void *os_path = utf8_to_filename((char *) dp->data.cFileName);
	    if (!os_path)
		return sys_seterror(L, ERROR_NOT_ENOUGH_MEMORY);

	    dp->h = is_WinNT
	     ? FindFirstFileW(os_path, &dp->data)
	     : FindFirstFileA(os_path, (WIN32_FIND_DATAA *) &dp->data);

	    free(os_path);
	}

	if (dp->h != INVALID_HANDLE_VALUE) {
	    dp->is_root = 0;
	    return 1;
	}
    }
#endif
    return sys_seterror(L, 0);
}

/*
 * Arguments: [directory (string)]
 * Returns: [dir_udata]
 */
static int
sys_dir (lua_State *L)
{
    struct dir *dp = lua_newuserdata(L, sizeof(struct dir));

    luaL_getmetatable(L, DIR_TYPENAME);
    lua_setmetatable(L, -2);
#ifndef _WIN32
    dp->data = NULL;
#else
    dp->h = INVALID_HANDLE_VALUE;
#endif
    return lua_gettop(L) ? sys_dir_open(L, 1, dp) : 1;
}

/*
 * Arguments: dir_udata
 */
static int
sys_dir_close (lua_State *L)
{
    struct dir *dp = checkudata(L, 1, DIR_TYPENAME);

#ifndef _WIN32
    if (dp->data) {
	closedir(dp->data);
	dp->data = NULL;
    }
#else
    if (dp->h != INVALID_HANDLE_VALUE) {
	FindClose(dp->h);
	dp->h = INVALID_HANDLE_VALUE;
    }
#endif
    return 0;
}

/*
 * Arguments: dir_udata, directory (string)
 *
 * Returns: [filename (string), is_directory (boolean)]
 * |
 * Returns (win32): [drive_letter (string: A .. Z), drive_type (string)]
 */
static int
sys_dir_next (lua_State *L)
{
    struct dir *dp = checkudata(L, 1, DIR_TYPENAME);

    if (lua_gettop(L) == 2) {  /* `for' start? */
	/* return generator (dir_udata) */
	lua_pushvalue(L, 1);
	return sys_dir_open(L, 2, dp);
    } else {  /* `for' step */
	char *filename;

#ifndef _WIN32
	struct dirent *entry;

	if (!dp->data)
	    return 0;
	do {
	    entry = readdir(dp->data);
	    if (!entry) {
		closedir(dp->data);
		dp->data = NULL;
		return 0;
	    }
	    filename = entry->d_name;
	} while (filename[0] == '.' && (filename[1] == '\0'
	 || (filename[1] == '.' && filename[2] == '\0')));

	lua_pushstring(L, filename);
	lua_pushboolean(L, entry->d_type & DT_DIR);
	return 2;
#else
	filename = (char *) dp->data.cFileName;

	if (dp->is_root) {
	    while (++*filename <= 'Z') {
		const char *type;

		switch (GetDriveTypeA(filename)) {
		case DRIVE_REMOVABLE:	type = "removable"; break;
		case DRIVE_FIXED:	type = "fixed"; break;
		case DRIVE_REMOTE:	type = "remote"; break;
		case DRIVE_CDROM:	type = "cdrom"; break;
		case DRIVE_RAMDISK:	type = "ramdisk"; break;
		default: continue;
		}
		lua_pushlstring(L, filename, 2);
		lua_pushstring(L, type);
		return 2;
	    }
	    return 0;
	}

	if (dp->h == INVALID_HANDLE_VALUE)
	    return 0;
	for (; ; ) {
	    int is_dots = 1;
	    {
		char *path = filename_to_utf8(filename);
		if (!path)
		    return sys_seterror(L, ERROR_NOT_ENOUGH_MEMORY);

		if (!(path[0] == '.' && (path[1] == '\0'
		 || (path[1] == '.' && path[2] == '\0')))) {
		    lua_pushstring(L, path);
		    lua_pushboolean(L,
		     dp->data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
		    is_dots = 0;
		}

		free(path);
	    }
	    if (is_WinNT
	     ? !FindNextFileW(dp->h, &dp->data)
	     : !FindNextFileA(dp->h, (WIN32_FIND_DATAA *) &dp->data)) {
		FindClose(dp->h);
		dp->h = INVALID_HANDLE_VALUE;
		return is_dots ? 0 : 2;
	    }
	    if (!is_dots) break;
	}
	return 2;
#endif
    }
}


#define FS_METHODS \
    {"stat",		sys_stat}, \
    {"statfs",		sys_statfs}, \
    {"remove",		sys_remove}, \
    {"rename",		sys_rename}, \
    {"realpath",	sys_realpath}, \
    {"curdir",		sys_curdir}, \
    {"mkdir",		sys_mkdir}, \
    {"rmdir",		sys_rmdir}, \
    {"dir",		sys_dir}

static luaL_reg dir_meth[] = {
    {"__call",		sys_dir_next},
    {"__gc",		sys_dir_close},
    {NULL, NULL}
};
