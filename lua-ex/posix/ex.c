#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include "spawn.h"


/* Generally useful function -- what luaL_checkudata() should do */
extern void *checkuserdata(lua_State *L, int idx, const char *tname)
{
    void *ud;
    luaL_argcheck(L, ud = luaL_checkudata(L, idx, tname), idx, tname);
    return ud;
}


/* -- nil error */
extern int push_error(lua_State *L)
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
	if (!val)
		return push_error(L);
	lua_pushstring(L, val);
	return 1;
}

/* name value -- true/nil error */
static int ex_setenv(lua_State *L)
{
	const char *nam = luaL_checkstring(L, 1);
	const char *val = luaL_checkstring(L, 2);
	if (-1 == setenv(nam, val, 1))
		return push_error(L);
	lua_pushboolean(L, 1);
	return 1;
}

/* name -- true/nil error */
static int ex_unsetenv(lua_State *L)
{
	const char *nam = luaL_checkstring(L, 1);
	if (-1 == unsetenv(nam))
		return push_error(L);
	lua_pushboolean(L, 1);
	return 1;
}

/* -- environment-table */
static int ex_environ(lua_State *L)
{
	const char *nam, *val, *end;
	const char **env;
	lua_newtable(L);
	for (env = (const char **)environ; (nam = *env); env++) {
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
		return push_error(L);
	lua_pushboolean(L, 1);
	return 1;
}

/* pathname -- true/nil error */
static int ex_mkdir(lua_State *L)
{
	const char *pathname = luaL_checkstring(L, 1);
	if (-1 == mkdir(pathname, 0777))
		return push_error(L);
	lua_pushboolean(L, 1);
	return 1;
}

/* -- pathname/nil error */
static int ex_currentdir(lua_State *L)
{
	char pathname[PATH_MAX + 1];
	if (!getcwd(pathname, sizeof pathname))
		return push_error(L);
	lua_pushstring(L, pathname);
	return 1;
}


/* pathname/file -- entry */
static int ex_dirent(lua_State *L)
{
	struct stat st;
	int ret;
	switch (lua_type(L, 1)) {
	default: return luaL_argerror(L, 1, "expected file or pathname");
	case LUA_TSTRING: {
		const char *name = lua_tostring(L, 1);
		ret = stat(name, &st);
		} break;
	case LUA_TUSERDATA: {
		FILE **pf = checkuserdata(L, 1, LUA_FILEHANDLE);
		ret = fstat(fileno(*pf), &st);
		} break;
	}
	if (ret == -1) return push_error(L);
	if (lua_type(L, 2) != LUA_TTABLE) {
		lua_newtable(L);
		lua_replace(L, 2);
	}
	lua_pushliteral(L, "type");
	if (S_ISDIR(st.st_mode))
		lua_pushliteral(L, "directory");
	else
		lua_pushliteral(L, "file");
	lua_settable(L, 2);
	lua_pushliteral(L, "size");
	lua_pushnumber(L, st.st_size);
	lua_settable(L, 2);
	lua_settop(L, 2);
	return 1;
}

#define DIR_HANDLE "DIR*"
struct dir_iter {
	DIR *dir;
	size_t pathlen;
	char pathname[PATH_MAX + 1];
};

static int dir_getpathname(lua_State *L, int index)
{
	struct dir_iter *pi = lua_touserdata(L, index);
	lua_pushlstring(L, pi->pathname, pi->pathlen);
	return 1;
}

static int dir_setpathname(lua_State *L, int index)
{
	struct dir_iter *pi = lua_touserdata(L, index);
	size_t len;
	const char *path = lua_tolstring(L, -1, &len);
	if (len >= sizeof pi->pathname - 1)
		return luaL_argerror(L, 1, "pathname too long");
	if (path[len - 1] != *LUA_DIRSEP) {
		lua_pushliteral(L, LUA_DIRSEP);
		lua_concat(L, 2);
		path = lua_tostring(L, -1);
		len++;
	}
	memcpy(pi->pathname, path, len + 1);
	pi->pathlen = len;
	lua_pop(L, 1);
	return 0;
}

/* pathname -- iter state nil */
/* {{ dir ... -- entry }} */
static int ex_dir(lua_State *L)
{
	const char *pathname;
	struct dir_iter *pi;
	struct dirent *d;
	switch (lua_type(L, 1)) {
	default: return luaL_argerror(L, 1, "expected pathname");
	case LUA_TSTRING:
		pathname = lua_tostring(L, 1);
		lua_pushcfunction(L, ex_dir);          /* pathname ... iter */
		pi = lua_newuserdata(L, sizeof *pi);   /* pathname ... iter state */
		pi->dir = opendir(pathname);
		if (!pi->dir) return push_error(L);
		luaL_getmetatable(L, DIR_HANDLE);      /* pathname ... iter state M */
		lua_setmetatable(L, -2);               /* pathname ... iter state */
		lua_pushvalue(L, 1);                   /* pathname ... iter state pathname */
		dir_setpathname(L, -2);                /* pathname ... iter state */
		return 2;
	case LUA_TUSERDATA:
		pi = checkuserdata(L, 1, DIR_HANDLE);
		d = readdir(pi->dir);
		if (!d) {
			closedir(pi->dir);
			return push_error(L);
		}
		lua_newtable(L);                       /* dir ... entry */
		dir_getpathname(L, 1);                 /* dir ... entry dirpath */
		lua_pushstring(L, d->d_name);          /* dir ... entry dirpath name */
		lua_pushliteral(L, "name");            /* dir ... entry dirpath name "name" */
		lua_pushvalue(L, -2);                  /* dir ... entry dirpath name "name" name */
		lua_settable(L, -5);                   /* dir ... entry dirpath name */
		lua_concat(L, 2);                      /* dir ... entry fullpath */
		lua_replace(L, 1);                     /* fullpath ... entry */
		lua_replace(L, 2);                     /* fullpath entry ... */
		return ex_dirent(L);
	}
	/*NOTREACHED*/
}


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
		return push_error(L);
	lua_pushboolean(L, 1);
	return 1;
}

/* file mode [offset [length]] -- true/nil error */
static int ex_lock(lua_State *L)
{
	FILE **pf = checkuserdata(L, 1, LUA_FILEHANDLE);
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


static int make_pipe(FILE **i, FILE **o)
{
	int fd[2];
	if (-1 == pipe(fd))
		return 0;
	*i = fdopen(fd[0], "r");
	*o = fdopen(fd[1], "w");
	return 1;
}

/* -- in out/nil error */
static int ex_pipe(lua_State *L)
{
	FILE *i, *o, **pf;
	if (!make_pipe(&i, &o))
		return push_error(L);
	luaL_getmetatable(L, LUA_FILEHANDLE);
	*(pf = lua_newuserdata(L, sizeof *pf)) = i;
	lua_pushvalue(L, -2);
	lua_setmetatable(L, -2);
	*(pf = lua_newuserdata(L, sizeof *pf)) = o;
	lua_pushvalue(L, -2);
	lua_setmetatable(L, -2);
	return 2;
}


/* filename [args-opts] -- true/nil error */
/* args-opts -- true/nil error */
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
				return luaL_error(L, "no command specified");
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
	default: return luaL_argerror(L, 2, "expected options table");
	case LUA_TNONE:
		spawn_param_defaults(&params);             /* cmd opts ... */
		break;
	case LUA_TTABLE:
		lua_getfield(L, 2, "args");                /* cmd opts ... argtab */
		switch (lua_type(L, -1)) {
		default: return luaL_error(L, "args option must be an array");
		case LUA_TNIL:
			lua_pop(L, 1);                         /* cmd opts ... */
			lua_pushvalue(L, 2);                   /* cmd opts ... opts */
			if (0) /*FALLTHRU*/
		case LUA_TTABLE:
			if (lua_objlen(L, 2) > 0)
				return luaL_error(L, "cannot specify both the args option and array values");
			spawn_param_args(&params);             /* cmd opts ... */
			break;
		}
		lua_getfield(L, 2, "env");                 /* cmd opts ... envtab */
		switch (lua_type(L, -1)) {
		default: return luaL_error(L, "env option must be a table");
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
	if (lua_isnil(L, -1)) return luaL_error(L, "io not loaded");
	luaL_openlib(L, 0, ex_iolib, 0);

	/* extend the os table */
	lua_getglobal(L, "os");
	if (lua_isnil(L, -1)) return luaL_error(L, "os not loaded");
	luaL_openlib(L, "os", ex_oslib, 0);

	/* extend the io.file metatable */
	luaL_getmetatable(L, LUA_FILEHANDLE);
	if (lua_isnil(L, -1)) return luaL_error(L, "can't find FILE* metatable");
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
