#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <spawn.h>
#include <sys/wait.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

extern char **environ;

/* helper functions */
static void *safe_alloc(lua_State *L, size_t n)
{
	return lua_newuserdata(L, n);
}

static int posix_error(lua_State *L)
{
	lua_pushnil(L);
	lua_pushstring(L, strerror(errno));
	return 2;
}


/* seconds -- */
static int osex_sleep(lua_State *L)
{
	lua_Number seconds = luaL_checknumber(L, 1);
	usleep(1e6 * seconds);
	return 0;
}


#define PROCESS_HANDLE "process"

struct process {
	pid_t pid;
	int status;
};

static void get_redirect(lua_State *L, posix_spawn_file_actions_t *file_actions,
	const char *stream, int descriptor)
{
	lua_getfield(L, 2, stream);
	if (!lua_isnil(L, -1))
		posix_spawn_file_actions_adddup2(file_actions, 
			fileno(luaL_checkudata(L, -1, LUA_FILEHANDLE)), descriptor);
	lua_pop(L, 1);
}

/* filename [options] -- true/nil,error */
static int osex_spawn(lua_State *L)
{
	const char *filename = luaL_checkstring(L, 1);
	posix_spawn_file_actions_t file_actions;
	char **argv;
	char **envp;
	struct process *p;
	if (lua_type(L, 2) == LUA_TTABLE) {
		lua_getfield(L, 2, "args");
		if (!lua_isnil(L, -1)) {
			luaL_checktype(L, -1, LUA_TTABLE);
			/* XXX */
		}
		else {
			argv = safe_alloc(L, 1 * sizeof *argv);
			argv[0] = 0;
		}
		lua_pop(L, 1);

		lua_getfield(L, 2, "env");
		if (!lua_isnil(L, -1)) {
			size_t i, n;
			luaL_checktype(L, -1, LUA_TTABLE);
			n = lua_objlen(L, -1);
			envp = safe_alloc(L, (n + 1) * sizeof *envp);
			for (i = 1; i <= n; i++) {
				lua_rawgeti(L, -1, i);
				luaL_checktype(L, -1, LUA_TSTRING);
				/* XXX push onto zz */
			}
		}
		else {
			envp = environ;
		}
		lua_pop(L, 1);

		get_redirect(L, &file_actions, "stdin", STDIN_FILENO);
		get_redirect(L, &file_actions, "stdout", STDOUT_FILENO);
		get_redirect(L, &file_actions, "stderr", STDERR_FILENO);
	}
	p = lua_newuserdata(L, sizeof *p);
	luaL_getmetatable(L, PROCESS_HANDLE);
	lua_setmetatable(L, -2);
	p->status = -1;
	if (0 != posix_spawnp(&p->pid, filename, &file_actions, 0, argv, envp))
		return posix_error(L);
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
