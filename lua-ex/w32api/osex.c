#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

extern char **environ;

/* helper functions */
static void *safe_alloc(lua_State *L, size_t n)
{
	return lua_newuserdata(L, n);
}

static int windows_error(lua_State *L)
{
	lua_pushnil(L);
	lua_pushstring(L, strerror(errno));
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
	HANDLE pid;
	int status;
};

static void get_redirect(lua_State *L, HANDLE *handle, const char *stream)
{
	lua_getfield(L, 2, stream);
	if (!lua_isnil(L, -1)) {
		FILE **pf = luaL_checkudata(L, LUA_FILEHANDLE);
		*handle = _get_osfhandle(_filen(*pf));
	}
	lua_pop(L, 1);
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
	cmdline = safe_alloc(len + 3);
	sprintf(cmdline, "\"%s\"", filename);
	environment = 0;
	si.cb = sizeof si;
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdInput  = GetStandardHandle(STD_INPUT_HANDLE);
	si.hStdOutput = GetStandardHandle(STD_OUTPUT_HANDLE);
	si.hStdError  = GetStandardHandle(STD_ERROR_HANDLE);
	if (lua_type(L, 2) == LUA_TTABLE) {
		lua_getfield(L, 2, "args");
		if (!lua_isnil(L, -1)) {
			luaL_checktype(L, -1, LUA_TTABLE);
			cmdline = insert_alli(L, -1);
		}
		lua_pop(L, 1);

		lua_getfield(L, 2, "env");
		if (!lua_isnil(L, -1)) {
			luaL_checktype(L, -1, LUA_TTABLE);
			environment = insert_all(L, -1);
		}
		lua_pop(L, 1);

		get_redirect(L, "stdin",  &si.hStdInput );
		get_redirect(L, "stdout", &si.hStdOutput);
		get_redirect(L, "stderr", &si.hStdError );
	}
	p = lua_newuserdata(L, sizeof *p);
	luaL_getmetatable(L, PROCESS_HANDLE);
	lua_setmetatable(L, -2);
	p->status = -1;
	if (!CreateProcess(0, cmdline, 0, 0, 0, 0, environment, 0, &si, &pi))
		return windows_error(L);
	p->pid = pi.hProcess;
	return 1;
}

static int process_wait(lua_State *L)
{
	struct process *p = luaL_checkudata(L, 1, PROCESS_HANDLE);
	if (p->status != -1) {
		int status;
		waitpid(p->pid, &status, 0);
		p->status = WEXITSTATUS(status);
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
	int fd[2];
	FILE **pf;
	if (0 != pipe(fd))
		return posix_error(L);
	luaL_getmetatable(L, LUA_FILEHANDLE);
	pf = lua_newuserdata(L, sizeof *pf);
	lua_pushvalue(L, -2);
	lua_setmetatable(L, -2);
	*pf = fdopen(fd[0], "r");
	pf = lua_newuserdata(L, sizeof *pf);
	lua_pushvalue(L, -2);
	lua_setmetatable(L, -2);
	*pf = fdopen(fd[1], "w");
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
