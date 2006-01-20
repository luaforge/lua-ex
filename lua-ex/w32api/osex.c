#include <stdio.h>
#include <ctype.h>
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

extern char **environ;

/* helper functions */
static void *safe_alloc(lua_State *L, void *p, size_t n)
#if 0
{
	void *q = realloc(p, n);
	if (!q) {
		realloc(p, 0);
		lua_error(L, "memory allocation error");
	}
	return q;
}
#else
{
	p = lua_newuserdata(L, n);
	lua_pop(L, 1);
	return p;
}
#endif


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


/* seconds -- */
static int osex_sleep(lua_State *L)
{
	lua_Number seconds = luaL_checknumber(L, 1);
	Sleep(1e3 * seconds);
	return 0;
}


#define PROCESS_HANDLE "process"

struct process {
	HANDLE hProcess;
	int status;
};

/*
extern long _get_osfhandle(int);
extern int _fileno(FILE *);
*/

static void get_redirect(lua_State *L, const char *stream, HANDLE *handle)
{
	lua_getfield(L, 2, stream);
	if (!lua_isnil(L, -1)) {
		FILE **pf = luaL_checkudata(L, -1, LUA_FILEHANDLE);
		*handle = (HANDLE)_get_osfhandle(_fileno(*pf));
	}
	lua_pop(L, 1);
}

int abs_index(lua_State *L, int idx)
{
	if (idx < 0)
		return lua_gettop(L) + idx + 1;
	return idx;
}

static char *insert_args(lua_State *L, int index, char *cmd, size_t cmdlen)
{
	size_t alloc = cmdlen + 1;
	size_t i, n = lua_objlen(L, index);
	index = abs_index(L, index);
	for (i = 1; i <= n; i++) {
		size_t arglen;
		const char *arg;
		lua_rawgeti(L, -1, i);
		arg = luaL_checklstring(L, -1, &arglen);
		if (cmdlen + arglen + 1 >= alloc)
			cmd = safe_alloc(L, cmd, alloc *= 2);
		cmdlen += sprintf(cmd + cmdlen, " \"%s\"", arg);
		lua_pop(L, 1);
	}
	return cmd;
}

static char *insert_env(lua_State *L, int index)
{
	char *env;
	size_t envlen, alloc;
	index = abs_index(L, index);
	envlen = 0;
	env = safe_alloc(L, env, alloc = 256);
	lua_pushnil(L);
	while (lua_next(L, -2)) {
		size_t namlen, vallen;
		const char *nam = luaL_checklstring(L, -2, &namlen);
		const char *val = luaL_checklstring(L, -2, &vallen);
		if (namlen + 1 + vallen + 2 >= alloc)
			env = safe_alloc(L, env, alloc *= 2);
		envlen += sprintf(env + envlen, "%s=%s", nam, val);
		env[envlen++] = '\0';
	}
	env[envlen] = '\0';
	return env;
}

/* filename [options] -- true/nil,error */
static int osex_spawn(lua_State *L)
{
	size_t len;
	const char *filename;
	char *cmdline;
	char *environment;
	struct process *p;
	STARTUPINFO si;
	PROCESS_INFORMATION pi;
	filename = luaL_checklstring(L, 1, &len);
	cmdline = safe_alloc(L, 0, len += 3);
	sprintf(cmdline, "\"%s\"", filename);
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
			cmdline = insert_args(L, -1, cmdline, len);
		}
		lua_pop(L, 1);

		lua_getfield(L, 2, "env");
		if (!lua_isnil(L, -1)) {
			luaL_checktype(L, -1, LUA_TTABLE);
			environment = insert_env(L, -1);
		}
		lua_pop(L, 1);

		get_redirect(L, "stdin",  &si.hStdInput);
		get_redirect(L, "stdout", &si.hStdOutput);
		get_redirect(L, "stderr", &si.hStdError);
	}
	p = lua_newuserdata(L, sizeof *p);
	luaL_getmetatable(L, PROCESS_HANDLE);
	lua_setmetatable(L, -2);
	p->status = -1;
	if (!CreateProcess(0, cmdline, 0, 0, 0, 0, environment, 0, &si, &pi))
		return windows_error(L);
	p->hProcess = pi.hProcess;
	return 1;
}

static int process_wait(lua_State *L)
{
	struct process *p = luaL_checkudata(L, 1, PROCESS_HANDLE);
	if (p->status != -1) {
		DWORD exitcode;
		WaitForSingleObject(p->hProcess, INFINITE);
		GetExitCodeProcess(p->hProcess, &exitcode);
		p->status = exitcode;
	}
	lua_pushnumber(L, p->status);
	return 1;
}

static const luaL_reg process_lib[] = {
	{"wait", process_wait},
	{0,0}
};


static int osex_newpipe(lua_State *L)
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


static const luaL_reg osex_lib[] = {
	{"sleep", osex_sleep},
	{"spawn", osex_spawn},
	{"newpipe", osex_newpipe},
	{0,0}
};

int luaopen_osex(lua_State *L)
{
	luaL_newmetatable(L, PROCESS_HANDLE);
	luaL_register(L, 0, process_lib);
	luaL_register(L, "os", osex_lib);
	return 1;
}
