#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <sys/locking.h>


/* Generally useful function -- what luaL_checkudata() should do */
static void *
luaL_checkuserdata(lua_State *L, int idx, const char *tname)
{
    void *ud;
    luaL_argcheck(L, ud = luaL_checkudata(L, idx, tname), idx, tname);
    return ud;
}


/* -- nil error */
static int windows_error(lua_State *L)
{
	DWORD error = GetLastError();
	char buffer[1024];
	size_t len = sprintf(buffer, "%lu (0x%lX): ", error, error);
	size_t res = FormatMessage(
			FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_SYSTEM,
			0, error, 0, buffer + len, sizeof buffer - len, 0);
	if (res) {
		len += res;
		while (len > 0 && isspace(buffer[len - 1]))
			len--;
	}
	else
		len += sprintf(buffer + len, "<error string not available>");
	lua_pushnil(L);
	lua_pushlstring(L, buffer, len);
	return 2;
}


/* -- environment-table */
static int ex_environ(lua_State *L)
{
	const char *envs, *nam, *val, *end;
	if (!(envs = GetEnvironmentStrings()))
		return windows_error(L);
	lua_newtable(L);
	for (nam = envs; *nam; nam = end + 1) {
		val = strchr(nam, '=');
		end = strchr(val, '\0');
		lua_pushlstring(L, nam, val - nam);
		val++;
		lua_pushlstring(L, val, end - val);
		lua_settable(L, -3);
	}
	return 1;
}

/* name -- value/nil */
static int ex_getenv(lua_State *L)
{
	const char *nam = luaL_checkstring(L, 1);
	char val[1024];
	size_t len;
	len = GetEnvironmentVariable(nam, val, sizeof val);
	if (sizeof val < len)
		return windows_error(L);
	else if (len == 0)
		lua_pushnil(L);
	else
		lua_pushlstring(L, val, len);
	return 1;
}

/* name value -- true/nil error */
static int ex_setenv(lua_State *L)
{
	const char *nam = luaL_checkstring(L, 1);
	const char *val = luaL_checkstring(L, 2);
	if (!SetEnvironmentVariable(nam, val))
		return windows_error(L);
	lua_pushboolean(L, 1);
	return 1;
}

/* name -- true/nil error */
static int ex_unsetenv(lua_State *L)
{
	const char *nam = luaL_checkstring(L, 1);
	if (!SetEnvironmentVariable(nam, 0))
		return windows_error(L);
	lua_pushboolean(L, 1);
	return 1;
}


/* seconds -- */
static int ex_sleep(lua_State *L)
{
	lua_Number seconds = luaL_checknumber(L, 1);
	Sleep(1e3 * seconds);
	return 0;
}


/* pathname -- true/nil error */
static int ex_chdir(lua_State *L)
{
	const char *pathname = luaL_checkstring(L, 1);
	if (!SetCurrentDirectory(pathname))
		return windows_error(L);
	lua_pushboolean(L, 1);
	return 1;
}

/* pathname -- true/nil error */
static int ex_mkdir(lua_State *L)
{
	const char *pathname = luaL_checkstring(L, 1);
	if (!CreateDirectory(pathname, 0))
		return windows_error(L);
	lua_pushboolean(L, 1);
	return 1;
}

/* -- pathname/nil error */
static int ex_currentdir(lua_State *L)
{
	char pathname[PATH_MAX + 1];
	size_t len;
	if (!(len = GetCurrentDirectory(sizeof pathname, pathname)))
		return windows_error(L);
	lua_pushlstring(L, pathname, len);
	return 1;
}


/* pathname -- iter state nil */
static int ex_dir(lua_State *L) { return luaL_error(L, "not yet implemented"); }

/* pathname -- entry */
static int ex_dirent(lua_State *L) { return luaL_error(L, "not yet implemented"); }


static int file_lock(lua_State *L, FILE *fh, const char *mode, long start, long len)
{
	int code;
    int lkmode;
    switch (*mode) {
        case 'r': lkmode = LK_NBLCK; break;
        case 'w': lkmode = LK_NBLCK; break;
        case 'u': lkmode = LK_UNLCK; break;
        default : return luaL_error (L, "invalid mode");
    }
    if (!len) {
        fseek (fh, 0L, SEEK_END);
        len = ftell (fh);
    }
    fseek (fh, start, SEEK_SET);
    code = _locking (fileno(fh), lkmode, len);
    if (code == -1) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, strerror(errno));
        return 2;
    }
    else {
        lua_pushboolean(L, 1);
        return 1;
    }
}

/* file mode [start [length]] -- true/nil error */
static int ex_lock(lua_State *L)
{
	FILE *f = luaL_checkuserdata(L, 1, LUA_FILEHANDLE);
	const char *mode = luaL_checkstring(L, 2);
	long start = luaL_optnumber(L, 3, 0);
	long length = luaL_optnumber(L, 4, 0);
	return file_lock(L, f, mode, start, length);
}

/* file [start [length]] -- true/nil error */
static int ex_unlock(lua_State *L)
{
	FILE *f = luaL_checkuserdata(L, 1, LUA_FILEHANDLE);
	long start = luaL_optnumber(L, 2, 0);
	long length = luaL_optnumber(L, 3, 0);
	return file_lock(L, f, "u", start, length);
}


/* -- in out/nil error */
static int ex_pipe(lua_State *L)
{
	HANDLE ph[2];
	FILE **pf;
	if (0 == CreatePipe(ph+0, ph+1, 0, 0))
		return windows_error(L);
	luaL_getmetatable(L, LUA_FILEHANDLE);  /* M */
	pf = lua_newuserdata(L, sizeof *pf);   /* M in */
	lua_pushvalue(L, -2);                  /* M in M */
	lua_setmetatable(L, -2);               /* M in */
	*pf = _fdopen(_open_osfhandle((long)ph[0], _O_RDONLY), "r");
	pf = lua_newuserdata(L, sizeof *pf);   /* M in out */
	lua_pushvalue(L, -3);                  /* M in out M */
	lua_setmetatable(L, -2);               /* M in out */
	*pf = _fdopen(_open_osfhandle((long)ph[1], _O_WRONLY), "w");
	return 2;
}


int abs_index(lua_State *L, int idx)
{
	return idx >= 0 ? idx : lua_gettop(L) + idx + 1;
}

static const char *concat_args(lua_State *L, int index)
{
	luaL_Buffer args;
	luaL_buffinit(L, &args);
	size_t i, n = lua_objlen(L, index);
	index = abs_index(L, index);
	for (i = 1; i <= n; i++) {
		int space;
		lua_rawgeti(L, index, i);
		space = 0 == strchr(luaL_checkstring(L, -1), ' ');
		luaL_putchar(&args, ' ');
		if (space) luaL_putchar(&args, '"');
		luaL_addvalue(&args);
		if (space) luaL_putchar(&args, '"');
	}
	lua_pop(L, n);
	luaL_pushresult(&args);
	return lua_tostring(L, -1);
}

static const char *concat_env(lua_State *L, int index)
{
	luaL_Buffer env;
	index = abs_index(L, index);
	luaL_buffinit(L, &env);
	lua_pushnil(L);              /* nil */
	while (lua_next(L, -2)) {    /* k v */
		/* Is the luaL_checktype() warning message useful here? */
		luaL_checktype(L, -2, LUA_TSTRING);
		luaL_checktype(L, -1, LUA_TSTRING);
		lua_pushvalue(L, -2);    /* k v k */
		luaL_addvalue(&env);
		luaL_putchar(&env, '=');
		lua_pop(L, 1);           /* k v */
		luaL_addvalue(&env);
		luaL_putchar(&env, '\0');
		lua_pop(L, 1);           /* k */
	}
	luaL_putchar(&env, '\0');
	luaL_pushresult(&env);       /* env */
	return lua_tostring(L, -1);
}

static void get_redirect(lua_State *L, const char *stream, HANDLE *handle)
{
	lua_getfield(L, 2, stream);
	if (!lua_isnil(L, -1)) {
		FILE **pf = luaL_checkuserdata(L, -1, LUA_FILEHANDLE);
		*handle = (HANDLE)_get_osfhandle(_fileno(*pf));
	}
	lua_pop(L, 1);
}

#define PROCESS_HANDLE "process"
struct process {
	HANDLE hProcess;
	int status;
};

/* filename [options] -- true/nil,error */
/* args-options -- true/nil,error */
static int ex_spawn(lua_State *L)
{
	const char *cmdline;
	const char *environment;
	struct process *p;
	STARTUPINFO si;
	PROCESS_INFORMATION pi;
	BOOL ret;
	/* XXX check for filename,options or just options */
	luaL_checktype(L, 1, LUA_TSTRING);
	cmdline = lua_tostring(L, 1);
	environment = 0;
	si.cb = sizeof si;
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
	si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
	si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);
	if (lua_type(L, 2) == LUA_TTABLE) {
		lua_getfield(L, 2, "args");
		if (!lua_isnil(L, -1)) {
			luaL_checktype(L, -1, LUA_TTABLE);
			lua_pushvalue(L, 1); /* push filename */
			concat_args(L, -2);
			lua_concat(L, 2); /* filename .. (" args")* */
			cmdline = lua_tostring(L, -1);
		}
		lua_getfield(L, 2, "env");
		if (!lua_isnil(L, -1)) {
			luaL_checktype(L, -1, LUA_TTABLE);
			environment = concat_env(L, -1);
		}
		get_redirect(L, "stdin",  &si.hStdInput);
		get_redirect(L, "stdout", &si.hStdOutput);
		get_redirect(L, "stderr", &si.hStdError);
	}
	p = lua_newuserdata(L, sizeof *p);
	luaL_getmetatable(L, PROCESS_HANDLE);
	lua_setmetatable(L, -2);
	p->status = -1;
	cmdline = strdup(cmdline);
	environment = strdup(environment);
	ret = CreateProcess(0, (char *)cmdline, 0, 0, 0, 0, (char *)environment, 0, &si, &pi);
	free((char *)cmdline);
	free((char *)environment);
	if (!ret)
		return windows_error(L);
	p->hProcess = pi.hProcess;
	return 1;
}

/* proc -- exitcode/nil error */
static int process_wait(lua_State *L)
{
	struct process *p = luaL_checkuserdata(L, 1, PROCESS_HANDLE);
	if (p->status != -1) {
		DWORD exitcode;
		WaitForSingleObject(p->hProcess, INFINITE);
		GetExitCodeProcess(p->hProcess, &exitcode);
		p->status = exitcode;
	}
	lua_pushnumber(L, p->status);
	return 1;
}


static const luaL_reg ex_iolib[] = {
	{"pipe",  ex_pipe},
	{0,0}
};
static const luaL_reg ex_iofile_methods[] = {
	{"lock",    ex_lock},
	{"unlock",  ex_unlock},
	{0,0}
};
static const luaL_reg ex_oslib[] = {
	{"getenv",     ex_getenv},
	{"setenv",     ex_setenv},
	{"unsetenv",   ex_unsetenv},
	{"environ",    ex_environ},

	{"sleep",      ex_sleep},

	{"chdir",      ex_chdir},
	{"mkdir",      ex_mkdir},
	{"currentdir", ex_currentdir},

	{"dir",        ex_dir},
	{"dirent",     ex_dirent},

	{"spawn",      ex_spawn},
	{0,0}
};
static const luaL_reg ex_process_methods[] = {
	{"wait", process_wait},
	{0,0}
};

int luaopen_ex(lua_State *L)
{

	/* extend the io table */
	lua_getglobal(L, "io");
	if (lua_isnil(L, -1)) luaL_error(L, "io not loaded");
	luaL_openlib(L, 0, ex_iolib, 0);

	/* extend the os table */
	lua_getglobal(L, "os");
	if (lua_isnil(L, -1)) luaL_error(L, "os not loaded");
	luaL_openlib(L, "os", ex_oslib, 0);

	/* extend the io.file metatable */
	luaL_getmetatable(L, LUA_FILEHANDLE);
	if (lua_isnil(L, -1)) luaL_error(L, "can't find FILE* metatable");
	luaL_openlib(L, 0, ex_iofile_methods, 0);

	/* proc metatable */
	luaL_newmetatable(L, PROCESS_HANDLE);       /* proc */
	luaL_openlib(L, 0, ex_process_methods, 0);  /* proc */
	lua_pushliteral(L, "__index");              /* proc __index */
	lua_pushvalue(L, -2);                       /* proc __index proc */
	lua_settable(L, -3);                        /* proc */

	/* for lack of a better thing to return */
	lua_pushboolean(L, 1);
	return 1;
}
