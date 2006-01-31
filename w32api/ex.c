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
static void *luaL_checkuserdata(lua_State *L, int idx, const char *tname)
{
    void *ud;
    luaL_argcheck(L, ud = luaL_checkudata(L, idx, tname), idx, tname);
    return ud;
}


/* -- nil error */
static int push_error(lua_State *L)
{
	DWORD error = GetLastError();
	char buffer[1024];
	size_t len = sprintf(buffer, "%lu (0x%lX): ", error, error);
	size_t res = FormatMessage(
			FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_SYSTEM,
			0, error, 0, buffer + len, sizeof buffer - len, 0);
	if (res) /* trim spaces */
		for (len += res; len > 0 && isspace(buffer[len - 1]); len--) ;
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
		return push_error(L);
	else if (len == 0) {
		lua_pushnil(L);
		return 1;
	}
	lua_pushlstring(L, val, len);
	return 1;
}

/* name value -- true/nil error */
static int ex_setenv(lua_State *L)
{
	const char *nam = luaL_checkstring(L, 1);
	const char *val = luaL_checkstring(L, 2);
	if (!SetEnvironmentVariable(nam, val))
		return push_error(L);
	lua_pushboolean(L, 1);
	return 1;
}

/* name -- true/nil error */
static int ex_unsetenv(lua_State *L)
{
	const char *nam = luaL_checkstring(L, 1);
	if (!SetEnvironmentVariable(nam, 0))
		return push_error(L);
	lua_pushboolean(L, 1);
	return 1;
}

/* -- environment-table */
static int ex_environ(lua_State *L)
{
	const char *nam, *val, *end;
	const char *envs = GetEnvironmentStrings();
	if (!envs) return push_error(L);
	lua_newtable(L);
	for (nam = envs; *nam; nam = end + 1) {
		end = strchr(val = strchr(nam, '=') + 1, '\0');
		lua_pushlstring(L, nam, val - nam - 1);
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
		return push_error(L);
	lua_pushboolean(L, 1);
	return 1;
}

/* pathname -- true/nil error */
static int ex_mkdir(lua_State *L)
{
	const char *pathname = luaL_checkstring(L, 1);
	if (!CreateDirectory(pathname, 0))
		return push_error(L);
	lua_pushboolean(L, 1);
	return 1;
}

/* -- pathname/nil error */
static int ex_currentdir(lua_State *L)
{
	char pathname[PATH_MAX + 1];
	size_t len = GetCurrentDirectory(sizeof pathname, pathname);
	if (len == 0) return push_error(L);
	lua_pushlstring(L, pathname, len);
	return 1;
}

static HANDLE get_handle(FILE *f)
{
	return (HANDLE)_get_osfhandle(fileno(f));
}

/* pathname -- iter state nil */
static int ex_dir(lua_State *L) { return luaL_error(L, "not yet implemented"); }

/* pathname/file -- entry */
static int ex_dirent(lua_State *L) { return luaL_error(L, "not yet implemented"); }


static int file_lock(lua_State *L, FILE *f, const char *mode, long offset, long length)
{
	HANDLE h = get_handle(f);
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
		return push_error(L);
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


/* -- LUA_FILEHANDLE file file */
static int make_pipe(lua_State *L, FILE *i, FILE *o)
{
	FILE **pf;
	luaL_getmetatable(L, LUA_FILEHANDLE);
	*(pf = lua_newuserdata(L, sizeof *pf)) = i;
	lua_pushvalue(L, -2);
	lua_setmetatable(L, -2);
	*(pf = lua_newuserdata(L, sizeof *pf)) = o;
	lua_pushvalue(L, -2);
	lua_setmetatable(L, -2);
	return 2;
}

/* -- in out/nil error */
static int ex_pipe(lua_State *L)
{
	HANDLE ph[2];
	if (0 == CreatePipe(ph+0, ph+1, 0, 0))
		return push_error(L);
	return make_pipe(L,
		 _fdopen(_open_osfhandle((long)ph[0], _O_RDONLY), "r"),
		 _fdopen(_open_osfhandle((long)ph[1], _O_WRONLY), "w"));
}


struct spawn_params {
	lua_State *L;
	const char *cmdline;
	const char *environment;
	STARTUPINFO si;
};

static int needs_quoting(const char *s)
{
	return s[0] != '"' && strchr(s, ' ');
}

/* filename ... */
static void spawn_param_filename(struct spawn_params *p)
{
	/* XXX luaL_checkstring is confusing here */
	p->cmdline = luaL_checkstring(p->L, 1);
	if (needs_quoting(p->cmdline)) {
		lua_pushliteral(p->L, "\"");   /* cmd ... q */
		lua_pushvalue(p->L, 1);        /* cmd ... q cmd */
		lua_pushvalue(p->L, -2);       /* cmd ... q cmd q */
		lua_concat(p->L, 3);           /* cmd ... "cmd" */
		lua_replace(p->L, 1);          /* "cmd" ... */
		p->cmdline = lua_tostring(p->L, 1);
	}
}

/* -- */
static void spawn_param_defaults(struct spawn_params *p)
{
	p->environment = 0;
	memset(&p->si, 0, sizeof p->si);
	p->si.cb = sizeof p->si;
}

/* cmd opts ... argtab -- cmd opts ... cmdline */
static void spawn_param_args(struct spawn_params *p)
{
	lua_State *L = p->L;
	luaL_Buffer args;
	luaL_buffinit(L, &args);
	size_t i, n = lua_objlen(L, -1);
	/* concatenate the arg array to a string */
	for (i = 1; i <= n; i++) {
		int quote;
		lua_rawgeti(L, -1, i);     /* ... argtab arg */
		/* XXX checkstring is confusing here */
		quote = needs_quoting(luaL_checkstring(L, -1));
		luaL_putchar(&args, ' ');
		if (quote) luaL_putchar(&args, '"');
		luaL_addvalue(&args);
		if (quote) luaL_putchar(&args, '"');
		lua_pop(L, 1);             /* ... argtab */
	}
	luaL_pushresult(&args);        /* ... argtab argstr */
	lua_pushvalue(L, 1);           /* cmd opts ... argtab argstr cmd */
	lua_replace(L, -2);            /* cmd opts ... argtab cmd argstr */
	lua_concat(L, 2);              /* cmd opts ... argtab cmdline */
	lua_replace(L, -2);            /* cmd opts ... cmdline */
	p->cmdline = lua_tostring(L, -1);
}

/* ... envtab */
static void spawn_param_env(struct spawn_params *p)
{
	lua_State *L = p->L;
	luaL_Buffer env;
	if (lua_isnil(L, -1)) {
		p->environment = 0;
		lua_pop(L, 1);
		return;
	}
	/* convert env table to zstring list */
	/* {nam1=val1,nam2=val2} => "nam1=val1\0nam2=val2\0\0" */
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
	luaL_pushresult(&env);         /* ... envtab envstr */
	lua_replace(L, -2);            /* ... envtab envstr */
	p->environment = lua_tostring(L, -1);
}

/* _ opts ... */
static int get_redirect(struct spawn_params *p, const char *stdname, HANDLE *ph)
{
	int ret;
	lua_getfield(p->L, 2, stdname);
	if ((ret = !lua_isnil(p->L, -1))) {
		/* XXX checkuserdata is confusing here */
		FILE **pf = luaL_checkuserdata(p->L, -1, LUA_FILEHANDLE);
		*ph = get_handle(*pf);
	}
	lua_pop(p->L, 1);
	return ret;
}

/* _ opts ... */
static void spawn_param_redirects(struct spawn_params *p)
{
	p->si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
	p->si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
	p->si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);
	if (get_redirect(p, "stdin",  &p->si.hStdInput)
		| get_redirect(p, "stdout", &p->si.hStdOutput)
		| get_redirect(p, "stderr", &p->si.hStdError))
		p->si.dwFlags = STARTF_USESTDHANDLES;
}

#define PROCESS_HANDLE "process"
struct process {
	int status;
	HANDLE hProcess;
};

static int spawn_param_execute(struct spawn_params *p, struct process *proc)
{
	char *c = strdup(p->cmdline);
	char *e = (char *)p->environment; // strdup(p->environment);
	PROCESS_INFORMATION pi;
	proc->status = -1;
	/* XXX does CreateProcess modify its environment argument? */
	int ret = CreateProcess(0, c, 0, 0, 0, 0, e, 0, &p->si, &pi);
	if (e) free(e);
	free(c);
	if (ret) proc->hProcess = pi.hProcess;
	return ret;
}

/* filename [args-opts] -- true/nil,error */
/* args-opts -- true/nil,error */
static int ex_spawn(lua_State *L)
{
	struct spawn_params params = {L};
	struct process *proc;

	if (lua_type(L, 1) == LUA_TTABLE) {
		lua_getfield(L, 1, "command");             /* opts ... cmd */
		if (!lua_isnil(L, -1)) {
			/* convert {command=command,arg1,...} to command {arg1,...} */
			lua_insert(L, 1);                      /* cmd opts ... */
		}
		else {
			/* convert {arg0,arg1,...} to arg0 {arg1,...} */
			size_t i, n = lua_objlen(L, 1);
			lua_rawgeti(L, 1, 1);                  /* opts ... nil cmd */
			if (lua_isnil(L, -1))
				luaL_error(L, "no command specified");
			/* XXX check LUA_TSTRING */
			lua_insert(L, 1);                      /* cmd opts ... nil */
			for (i = 2; i <= n; i++) {
				lua_rawgeti(L, 2, i);              /* cmd opts ... nil argi */
				lua_rawseti(L, 2, i - 1);          /* cmd opts ... nil */
			}
			lua_rawseti(L, 2, n);                  /* cmd opts ... */
		}
	}

	/* get filename to execute */
	spawn_param_filename(&params);

	/* get arguments, environment, and redirections */
	switch (lua_type(L, 2)) {
	default: luaL_argerror(L, 2, "expected options table"); break;
	case LUA_TNONE:
		spawn_param_defaults(&params);             /* cmd opts ... */
		break;
	case LUA_TTABLE:
		lua_getfield(L, 2, "args");                /* cmd opts ... argtab */
		switch (lua_type(L, -1)) {
		default: luaL_error(L, "args option must be an array"); break;
		case LUA_TNIL:
			lua_pop(L, 1);                         /* cmd opts ... */
			lua_pushvalue(L, 2);                   /* cmd opts ... opts */
			if (0) /*FALLTHRU*/
		case LUA_TTABLE:
			if (lua_objlen(L, 2) > 0)
				luaL_error(L, "cannot specify both the args option and array values");
			spawn_param_args(&params);             /* cmd opts ... */
			break;
		}
		lua_getfield(L, 2, "env");                 /* cmd opts ... envtab */
		switch (lua_type(L, -1)) {
		default: luaL_error(L, "env option must be a table"); break;
		case LUA_TNIL:
		case LUA_TTABLE:
			spawn_param_env(&params);              /* cmd opts ... */
			break;
		}
		spawn_param_redirects(&params);            /* cmd opts ... */
		break;
	}
	proc = lua_newuserdata(L, sizeof *proc);       /* cmd opts ... proc */
	luaL_getmetatable(L, PROCESS_HANDLE);          /* cmd opts ... proc M */
	lua_setmetatable(L, -2);                       /* cmd opts ... proc */
	if (!spawn_param_execute(&params, proc))
		return push_error(L);
	return 1; /* ... proc */
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
