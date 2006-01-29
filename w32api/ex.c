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

#define debug(...) fprintf(stderr,__VA_ARGS__)


/* Generally useful function -- what luaL_checkudata() should do */
static void *luaL_checkuserdata(lua_State *L, int idx, const char *tname)
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

static HANDLE get_handle(FILE *f)
{
	return (HANDLE)_get_osfhandle(fileno(f));
}

/* pathname -- iter state nil */
static int ex_dir(lua_State *L) { return luaL_error(L, "not yet implemented"); }

/* pathname -- entry */
/* XXX io.file -- entry */
static int ex_dirent(lua_State *L) { return luaL_error(L, "not yet implemented"); }


static int file_lock(lua_State *L, FILE *fh, const char *mode, long offset, long length)
{
	HANDLE h = get_handle(fh);
	DWORD flags;
	LARGE_INTEGER len = {0};
	OVERLAPPED ov = {0};
	BOOL ret;
	if (length) len.LowPart = length;
	else len.LowPart = GetFileSize(h, &len.HighPart);
	ov.Offset = offset;
	switch (*mode) {
		case 'w': flags = LOCKFILE_EXCLUSIVE_LOCK; /*FALLTHRU*/
		case 'r': flags |= LOCKFILE_FAIL_IMMEDIATELY; break;
		case 'u': flags = 0; break;
		default: return luaL_error(L, "invalid mode");
	}
	ret = flags ? LockFileEx(h, flags, 0, len.LowPart, len.HighPart, &ov)
		: UnlockFileEx(h, 0, len.LowPart, len.HighPart, &ov);
	if (!ret)
		return windows_error(L);
	lua_pushboolean(L, 1);
	return 1;
}

static int file_lock_crt(lua_State *L, FILE *fh, const char *mode, long offset, long length)
{
	int code;
    int lkmode;
    switch (*mode) {
        case 'r': lkmode = LK_NBRLCK; break;
        case 'w': lkmode = LK_NBLCK; break;
        case 'u': lkmode = LK_UNLCK; break;
        default : return luaL_error (L, "invalid mode");
    }
    if (!length) {
        fseek (fh, 0L, SEEK_END);
        length = ftell (fh);
    }
    fseek (fh, offset, SEEK_SET);
    code = _locking (fileno(fh), lkmode, length);
    if (code == -1) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, strerror(errno));
        return 2;
    }
	lua_pushboolean(L, 1);
	return 1;
}

/* file mode [offset [length]] -- true/nil error */
static int ex_lock(lua_State *L)
{
	FILE **pf = luaL_checkuserdata(L, 1, LUA_FILEHANDLE);
	const char *mode = luaL_checkstring(L, 2);
	long offset = luaL_optnumber(L, 3, 0);
	long length = luaL_optnumber(L, 4, 0);
	return file_lock(L, *pf, mode, offset, length);
}

/* file [offset [length]] -- true/nil error */
static int ex_unlock(lua_State *L)
{
	lua_pushliteral(L, "u");
	lua_insert(L, 2);
	return ex_lock(L);
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

static int needs_quoting(const char *s)
{
	return s[0] != '"' && strchr(s, ' ');
}

/* {arg1,arg 2} => " arg1 \"arg2\"" */
static const char *concat_args(lua_State *L)
{
	luaL_Buffer args;
	luaL_buffinit(L, &args);
	size_t i, n = lua_objlen(L, -1);
	for (i = 1; i <= n; i++) {
		int quote;
		lua_rawgeti(L, -1, i);         /* ... argtab arg */
		/* XXX checkstring is confusing here */
		quote = needs_quoting(luaL_checkstring(L, -1));
		luaL_putchar(&args, ' ');
		if (quote) luaL_putchar(&args, '"');
		luaL_addvalue(&args);
		if (quote) luaL_putchar(&args, '"');
		lua_pop(L, 1);                 /* ... argtab */
	}
	lua_pop(L, 1);                     /* ... */
	luaL_pushresult(&args);            /* ... argstr */
	return lua_tostring(L, -1);
}

/* {nam1=val1,nam2=val2} => "nam1=val1\0nam2=val2\0" */
static const char *concat_env(lua_State *L)
{
	luaL_Buffer env;
	luaL_buffinit(L, &env);
	lua_pushnil(L);                /* ... envtab nil */
	while (lua_next(L, -2)) {      /* ... envtab k v */
		/* XXX luaL_checktype is confusing here */
		luaL_checktype(L, -2, LUA_TSTRING);
		luaL_checktype(L, -1, LUA_TSTRING);
		lua_pushvalue(L, -2);      /* ... envtab k v k */
		luaL_addvalue(&env);
		luaL_putchar(&env, '=');
		lua_pop(L, 1);             /* ... envtab k v */
		luaL_addvalue(&env);
		luaL_putchar(&env, '\0');
		lua_pop(L, 1);             /* ... envtab k */
	}
	luaL_putchar(&env, '\0');
	lua_pop(L, 1);                 /* ... */
	luaL_pushresult(&env);         /* ... envstr */
	return lua_tostring(L, -1);
}

/* XXX document me */
static int get_redirect(lua_State *L, const char *stdname, HANDLE *ph)
{
	int ret;
	lua_getfield(L, 2, stdname);
	if ((ret = !lua_isnil(L, -1))) {
		/* XXX checkuserdata is confusing here */
		FILE **pf = luaL_checkuserdata(L, -1, LUA_FILEHANDLE);
		*ph = get_handle(*pf);
	}
	lua_pop(L, 1);
	return ret;
}

#define PROCESS_HANDLE "process"
struct process {
	HANDLE hProcess;
	int status;
};

/* filename [args-opts] -- true/nil,error */
/* args-opts -- true/nil,error */
static int ex_spawn(lua_State *L)
{
	const char *cmdline;
	const char *environment;
	struct process *p;
	STARTUPINFO si = {sizeof si};
	PROCESS_INFORMATION pi;
	BOOL ret;

	if (lua_type(L, 1) == LUA_TTABLE) {
		lua_getfield(L, 1, "command");             /* opts cmd */
		if (!lua_isnil(L, -1)) {
			/* convert {command=command,arg1,...} to command {arg1,...} */
			lua_insert(L, 1);                      /* cmd opts */
		}
		else {
			/* convert {arg0,arg1,...} to arg0 {arg1,...} */
			size_t i, n = lua_objlen(L, 1);
			lua_rawgeti(L, 1, 1);                  /* opts nil cmd */
			if (lua_isnil(L, -1))
				luaL_error(L, "no command specified");
			/* XXX check LUA_TSTRING */
			lua_insert(L, 1);                      /* cmd opts nil */
			for (i = 2; i <= n; i++) {
				lua_rawgeti(L, 2, i);              /* cmd opts nil argi */
				lua_rawseti(L, 2, i - 1);          /* cmd opts nil */
			}
			lua_rawseti(L, 2, n);                  /* cmd opts */
		}
	}

	/* get command */
	/* XXX luaL_checkstring is confusing here */
	cmdline = luaL_checkstring(L, 1);
	if (needs_quoting(cmdline)) {
		lua_pushliteral(L, "\"");          /* cmd ... q */
		lua_pushvalue(L, 1);               /* cmd ... q cmd */
		lua_pushvalue(L, -2);              /* cmd ... q cmd q */
		lua_concat(L, 3);                  /* cmd ... "cmd" */
		lua_replace(L, 1);                 /* "cmd" ... */
	}

	/* set defaults */
	environment = 0;
	si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
	si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
	si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);

	/* get arguments, environment, and redirections */
	switch (lua_type(L, 2)) {
	default: luaL_argerror(L, 2, "expected options table"); break;
	case LUA_TNONE: break;
	case LUA_TTABLE:
		lua_getfield(L, 2, "args");                /* cmd opts ... argtab */
		switch (lua_type(L, -1)) {
		default: luaL_error(L, "args option must be an array"); break;
		case LUA_TNIL:
			lua_pop(L, 1);                         /* cmd opts ... */
			if (lua_objlen(L, 2) == 0) break;
			lua_pushvalue(L, 2);                   /* cmd opts ... opts */
			if (0) /*FALLTHRU*/
		case LUA_TTABLE:
			if (lua_objlen(L, 2) > 0)
				luaL_error(L, "cannot specify both the args option and array values");
			concat_args(L);                        /* cmd opts ... argstr */
			lua_pushvalue(L, 1);                   /* cmd opts ... argstr cmd */
			lua_replace(L, -2);                    /* cmd opts ... cmd argstr */
			lua_concat(L, 2);                      /* cmd opts ... cmdline */
			cmdline = lua_tostring(L, -1);
			break;
		}
		lua_getfield(L, 2, "env");                 /* cmd opts ... envtab */
		switch (lua_type(L, -1)) {
		default: luaL_error(L, "env option must be a table"); break;
		case LUA_TNIL:
			lua_pop(L, 1);                         /* cmd opts */
			break;
		case LUA_TTABLE:
			environment = concat_env(L);           /* cmd opts ... envstr */
			break;
		}
		if (get_redirect(L, "stdin",  &si.hStdInput)       /* cmd opts ... in? */
		    | get_redirect(L, "stdout", &si.hStdOutput)    /* cmd opts ... out? */
		    | get_redirect(L, "stderr", &si.hStdError))    /* cmd opts ... err? */
			si.dwFlags = STARTF_USESTDHANDLES;
		break;
	}

	p = lua_newuserdata(L, sizeof *p);             /* cmd opts ... proc */
	luaL_getmetatable(L, PROCESS_HANDLE);          /* cmd opts ... proc M */
	lua_setmetatable(L, -2);                       /* cmd opts ... proc */
	p->status = -1;

	/* XXX does CreateProcess modify its environment argument? */
	cmdline = strdup(cmdline);
	if (environment) environment = strdup(environment);
	debug("CreateProcess(%s)\n", cmdline);
	ret = CreateProcess(0, (char *)cmdline, 0, 0, 0, 0, (char *)environment, 0, &si, &pi);
//	if (environment) free((char *)environment);
//	free((char *)cmdline);

	if (!ret)
		return windows_error(L);
	p->hProcess = pi.hProcess;
	return 1;  /* ... proc */
}

/* proc -- exitcode/nil error */
static int process_wait(lua_State *L)
{
	struct process *p = luaL_checkuserdata(L, 1, PROCESS_HANDLE);
	if (p->status == -1) {
		DWORD exitcode;
		WaitForSingleObject(p->hProcess, INFINITE);
		GetExitCodeProcess(p->hProcess, &exitcode);
		p->status = exitcode;
	}
	lua_pushnumber(L, p->status);
	return 1; /* exitcode */
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
