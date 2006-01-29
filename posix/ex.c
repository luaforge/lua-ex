#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <unistd.h>
#include <fcntl.h>
#include "spawn.h"
#include <sys/wait.h>


/* Generally useful function -- what luaL_checkudata() should do */
static void *luaL_checkuserdata(lua_State *L, int idx, const char *tname)
{
    void *ud;
    luaL_argcheck(L, ud = luaL_checkudata(L, idx, tname), idx, tname);
    return ud;
}


/* -- nil error */
static int posix_error(lua_State *L)
{
	lua_pushnil(L);
	lua_pushstring(L, strerror(errno));
	return 2;
}


/* name -- value/nil */
static int ex_getenv(lua_State *L)
{
	const char *nam = luaL_checkstring(L, 1);
	char *val = getenv(nam);
	if (val)
		lua_pushstring(L, val);
	else
		lua_pushnil(L);
	return 1;
}

/* name value -- true/nil error */
static int ex_setenv(lua_State *L)
{
	const char *nam = luaL_checkstring(L, 1);
	const char *val = luaL_checkstring(L, 2);
	if (-1 == setenv(nam, val, 1))
		return posix_error(L);
	lua_pushboolean(L, 1);
	return 1;
}

/* name -- true/nil error */
static int ex_unsetenv(lua_State *L)
{
	const char *nam = luaL_checkstring(L, 1);
	if (-1 == unsetenv(nam))
		return posix_error(L);
	lua_pushboolean(L, 1);
	return 1;
}

/* -- environment-table */
static int ex_environ(lua_State *L)
{
	char **env;
	const char *nam, *val, *end;
	lua_newtable(L);
	for (env = environ; (nam = *env); env++) {
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
	usleep(1e6 * seconds);
	return 0;
}


/* pathname -- true/nil error */
static int ex_chdir(lua_State *L)
{
	const char *pathname = luaL_checkstring(L, 1);
	if (-1 == chdir(pathname))
		return posix_error(L);
	lua_pushboolean(L, 1);
	return 1;
}

/* pathname -- true/nil error */
static int ex_mkdir(lua_State *L)
{
	const char *pathname = luaL_checkstring(L, 1);
	if (-1 == mkdir(pathname, 0777))
		return posix_error(L);
	lua_pushboolean(L, 1);
	return 1;
}

/* -- pathname/nil error */
static int ex_currentdir(lua_State *L)
{
	char pathname[PATH_MAX + 1];
	if (!getcwd(pathname, sizeof pathname))
		return posix_error(L);
	lua_pushstring(L, pathname);
	return 1;
}

/* pathname -- iter state nil */
static int ex_dir(lua_State *L) { return luaL_error(L, "not yet implemented"); }

/* pathname/file -- entry */
static int ex_dirent(lua_State *L) { return luaL_error(L, "not yet implemented"); }


static int file_lock(lua_State *L, FILE *f, const char *mode, long offset, long length)
{
	struct flock k;
	switch (*mode) {
		case 'w': k.l_type = F_WRLCK; break;
		case 'r': k.l_type = F_RDLCK; break;
		case 'u': k.l_type = F_UNLCK; break;
		default: return luaL_error(L, "invalid mode");
	}
	k.l_whence = SEEK_SET;
	k.l_start = offset;
	k.l_len = length;
	if (-1 == fcntl(fileno(f), F_SETLK, &k))
		return posix_error(L);
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
	int fd[2];
	if (-1 == pipe(fd))
		return posix_error(L);
	return make_pipe(L, fdopen(fd[0], "r"), fdopen(fd[1], "w"));
}


/* ... argtab -- ... */
static const char **build_args(lua_State *L)
{
	size_t i, n = lua_objlen(L, -1);
	const char **argv = lua_newuserdata(L, (n + 2) * sizeof *argv);
	                                   /* ... argtab argv */
	for (i = 0; i <= n; i++) {
		lua_rawgeti(L, -2, i);         /* ... argtab argv argi */
		argv[i] = lua_tostring(L, -1);
		lua_pop(L, 1);                 /* ... argtab argv */
	}
	argv[n + 1] = 0;
	lua_replace(L, -2);                /* ... argv */
	return argv;
}

/* ... envtab -- */
static const char **build_env(lua_State *L)
{
	size_t i = 0;
	luaL_Buffer estr;
	luaL_buffinit(L, &estr);
	lua_newtable(L);                   /* ... envtab a */
	lua_pushnil(L);                    /* ... envtab a nil */
	for (i = 0; lua_next(L, -3); i++) {/* ... envtab a k v */
		luaL_prepbuffer(&estr);
		lua_pushvalue(L, -2);          /* ... envtab a k v k */
		luaL_addvalue(&estr);
		luaL_putchar(&estr, '=');
		lua_pop(L, 1);                 /* ... envtab a k v */
		luaL_addvalue(&estr);
		lua_pop(L, 1);                 /* ... envtab a k */
		luaL_pushresult(&estr);        /* ... envtab a k estr */
		lua_rawseti(L, -3, i);         /* ... envtab a[n]=estr k */
	}                                  /* ... envtab a */
	lua_replace(L, -2);                /* ... a */
	return build_args(L);              /* ... */
}

static void get_redirect(lua_State *L, posix_spawn_file_actions_t *file_actions,
	const char *stream, int descriptor)
{
	lua_getfield(L, 2, stream);
	if (!lua_isnil(L, -1))
		posix_spawn_file_actions_adddup2(file_actions, 
			fileno(luaL_checkudata(L, -1, LUA_FILEHANDLE)), descriptor);
	lua_pop(L, 1);
}

#define PROCESS_HANDLE "process"
struct process {
	pid_t pid;
	int status;
};

/* filename [args-opts] -- true/nil,error */
/* args-opts -- true/nil,error */
static int ex_spawn(lua_State *L)
{
	const char *command;
	posix_spawn_file_actions_t file_actions;
	const char **argv;
	const char **envp;
	struct process *p;
	int ret;

	if (lua_type(L, 1) == LUA_TTABLE) {
		lua_getfield(L, 1, "command");             /* opts ... cmd */
		if (!lua_isnil(L, -1)) {
			/* convert {command=command,arg1,...} to command {arg1,...} */
			lua_insert(L, 1);                      /* cmd opts ... */
		}
		else {
			/* convert {arg0,arg1,...} to arg0 {arg1,...} */
			int i, n = lua_objlen(L, 1);
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

	/* XXX confusing */
	command = luaL_checkstring(L, 1);

	/* set defaults */
	argv = 0;
	envp = (const char **)environ;
	posix_spawn_file_actions_init(&file_actions);

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
			lua_pushvalue(L, 2);                   /* cmd opts ... opts */
			if (0) /*FALLTHRU*/
		case LUA_TTABLE:
			if (lua_objlen(L, 2) > 0)
				luaL_error(L, "cannot specify both the args option and array values");
			argv = build_args(L);                  /* cmd opts ... */
			if (!argv[0]) argv[0] = command;
		}
		lua_getfield(L, 2, "env");
		switch (lua_type(L, -1)) {
		default: luaL_error(L, "env option must be a table"); break;
		case LUA_TNIL: break;
		case LUA_TTABLE:
			envp = build_env(L);                   /* cmd opts ... */
		}
		get_redirect(L, &file_actions, "stdin", STDIN_FILENO);
		get_redirect(L, &file_actions, "stdout", STDOUT_FILENO);
		get_redirect(L, &file_actions, "stderr", STDERR_FILENO);
	}
	p = lua_newuserdata(L, sizeof *p);             /* cmd opts ... proc */
	luaL_getmetatable(L, PROCESS_HANDLE);          /* cmd opts ... proc M */
	lua_setmetatable(L, -2);                       /* cmd opts ... proc */
	p->status = -1;
	ret = posix_spawnp(&p->pid, command, &file_actions, 0, (char *const *)argv, (char *const *)envp);
	posix_spawn_file_actions_destroy(&file_actions);
	if (-1 == ret)
		return posix_error(L);
	return 1; /* ... proc */
}

/* proc -- exitcode/nil error */
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

