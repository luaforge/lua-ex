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


#define absindex(L,i) ((i)>0?(i):lua_gettop(L)+(i)+1)

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


FILE *check_file(lua_State *L, int idx, const char *argname)
{
	FILE **pf;
	if (idx > 0) pf = luaL_checkudata(L, idx, LUA_FILEHANDLE);
	else {
		idx = absindex(idx);
		pf = lua_touserdata(L, idx);
		luaL_getmetatable(L, LUA_FILEHANDLE);
		if (!pf || !lua_getmetatable(L, idx) || !lua_rawequal(L, -1, -2))
			luaL_error(L, "%s option: expected %s, got %s",
				argname, LUA_FILEHANDLE, luaL_typename(L, idx));
		lua_pop(L, 2);
	}
	if (!*pf) return luaL_error(L, "attempt to use a closed file"), NULL;
	return *pf;
}

/* pathname/file -- entry */
static int ex_dirent(lua_State *L)
{
	struct stat st;
	switch (lua_type(L, 1)) {
	default: return luaL_argerror(L, 1, "expected file or pathname");
	case LUA_TSTRING: {
		const char *name = lua_tostring(L, 1);
		if (-1 == stat(name, &st))
			return push_error(L);
		} break;
	case LUA_TUSERDATA: {
		FILE *f = check_file(L, 1, NULL);
		if (-1 == fstat(fileno(f), &st))
			return push_error(L);
		} break;
	}
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
struct diriter {
	DIR *dir;
};

/* ...diriter... -- ...diriter... pathname */
static int diriter_getpathname(lua_State *L, int index)
{
	lua_pushvalue(L, index);
	lua_gettable(L, LUA_REGISTRYINDEX);
	return 1;
}

/* ...diriter... pathname -- ...diriter... */
static int diriter_setpathname(lua_State *L, int index)
{
	size_t len;
	const char *path = lua_tolstring(L, -1, &len);
	if (path && path[len - 1] != *LUA_DIRSEP) {
		lua_pushliteral(L, LUA_DIRSEP);
		lua_concat(L, 2);
	}
	lua_pushvalue(L, index);            /* ... pathname diriter */
	lua_insert(L, -2);                  /* ... diriter pathname */
	lua_settable(L, LUA_REGISTRYINDEX); /* ... */
	return 0;
}

/* diriter -- diriter */
static int diriter_close(lua_State *L)
{
	struct diriter *pi = lua_touserdata(L, 1);
	if (pi->dir) {
		closedir(pi->dir);
		pi->dir = 0;
		lua_pushnil(L);
		diriter_setpathname(L, 1);
	}
	return 0;
}

/* pathname -- iter state nil */
/* diriter ... -- entry */
static int ex_dir(lua_State *L)
{
	const char *pathname;
	struct diriter *pi;
	struct dirent *d;
	switch (lua_type(L, 1)) {
	default: return luaL_argerror(L, 1, "expected pathname");
	case LUA_TSTRING:
		pathname = lua_tostring(L, 1);
		lua_pushcfunction(L, ex_dir);       /* pathname ... iter */
		pi = lua_newuserdata(L, sizeof *pi);/* pathname ... iter state */
		pi->dir = opendir(pathname);
		if (!pi->dir) return push_error(L);
		luaL_getmetatable(L, DIR_HANDLE);   /* pathname ... iter state M */
		lua_setmetatable(L, -2);            /* pathname ... iter state */
		lua_pushvalue(L, 1);                /* pathname ... iter state pathname */
		diriter_setpathname(L, -2);         /* pathname ... iter state */
		return 2;
	case LUA_TUSERDATA:
		pi = luaL_checkudata(L, 1, DIR_HANDLE);
		d = readdir(pi->dir);
		if (!d) {
			closedir(pi->dir);
			pi->dir = 0;
			return push_error(L);
		}
		lua_newtable(L);                    /* diriter ... entry */
		diriter_getpathname(L, 1);          /* diriter ... entry dirpath */
		lua_pushstring(L, d->d_name);       /* diriter ... entry dirpath name */
		lua_pushliteral(L, "name");         /* diriter ... entry dirpath name "name" */
		lua_pushvalue(L, -2);               /* diriter ... entry dirpath name "name" name */
		lua_settable(L, -5);                /* diriter ... entry dirpath name */
		lua_concat(L, 2);                   /* diriter ... entry fullpath */
		lua_replace(L, 1);                  /* fullpath ... entry */
		lua_replace(L, 2);                  /* fullpath entry ... */
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
	FILE *f = check_file(L, 1, NULL);
	const char *mode = luaL_checkstring(L, 2);
	long offset = luaL_optnumber(L, 3, 0);
	long length = luaL_optnumber(L, 4, 0);
	return file_lock(L, f, mode, offset, length);
}

/* file [offset [length]] -- true/nil error */
static int ex_unlock(lua_State *L)
{
	lua_pushliteral(L, "u");
	lua_insert(L, 2);
	return ex_lock(L);
}

static int closeonexec(int d)
{
	int fl = fcntl(d, F_GETFD);
	if (fl != -1)
		fl = fcntl(d, F_SETFD, fl | FD_CLOEXEC);
	return fl;
}

static int make_pipe(FILE **i, FILE **o)
{
	int fd[2];
	if (-1 == pipe(fd))
		return 0;
	closeonexec(fd[0]);
	closeonexec(fd[1]);
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
	debug("make_pipe returns %p<-%p\n", i, o);
	luaL_getmetatable(L, LUA_FILEHANDLE);       /* M */
	*(pf = lua_newuserdata(L, sizeof *pf)) = i; /* M i */
	lua_pushvalue(L, -2);                       /* M i M */
	lua_setmetatable(L, -2);                    /* M i */
	*(pf = lua_newuserdata(L, sizeof *pf)) = o; /* M i o */
	lua_pushvalue(L, -3);                       /* M i o M */
	lua_setmetatable(L, -2);                    /* M i o */
	return 2;
}


static void get_redirect(lua_State *L, int idx, const char *stdname, struct spawn_params *p)
{
	lua_getfield(L, idx, stdname);
	if (!lua_isnil(L, -1))
		spawn_param_redirect(p, stdname, check_file(L, -1, stdname));
	lua_pop(L, 1);
}

/* filename [args-opts] -- true/nil error */
/* args-opts -- true/nil error */
static int ex_spawn(lua_State *L)
{
	struct spawn_params *params = spawn_param_init(L);

	if (lua_type(L, 1) == LUA_TTABLE) {
		lua_getfield(L, 1, "command");          /* opts ... cmd */
		if (!lua_isnil(L, -1)) {
			/* convert {command=command,arg1,...} to command {arg1,...} */
			lua_insert(L, 1);                   /* cmd opts ... */
		}
		else {
			/* convert {arg0,arg1,...} to arg0 {arg1,...} */
			size_t i, n = lua_objlen(L, 1);
			lua_rawgeti(L, 1, 1);               /* opts ... nil cmd */
			lua_insert(L, 1);                   /* cmd opts ... nil */
			for (i = 2; i <= n; i++) {
				lua_rawgeti(L, 2, i);           /* cmd opts ... nil argi */
				lua_rawseti(L, 2, i - 1);       /* cmd opts ... nil */
			}
			lua_rawseti(L, 2, n);               /* cmd opts ... */
		}
	}

	/* get filename to execute */
	if (lua_type(L, 1) != LUA_TSTRING)
		return luaL_error(L, "command option: expected string, got %s", luaL_typename(L, 1));
	spawn_param_filename(params, lua_tostring(L, 1));

	/* get arguments, environment, and redirections */
	switch (lua_type(L, 2)) {
	default: return luaL_argerror(L, 2, "expected options table");
	case LUA_TNONE: break;
	case LUA_TTABLE:
		lua_getfield(L, 2, "args");             /* cmd opts ... argtab */
		switch (lua_type(L, -1)) {
		default: return luaL_error(L, "args option must be an array");
		case LUA_TNIL:
			lua_pop(L, 1);                      /* cmd opts ... */
			lua_pushvalue(L, 2);                /* cmd opts ... opts */
			if (0) /*FALLTHRU*/
		case LUA_TTABLE:
			if (lua_objlen(L, 2) > 0)
				return luaL_error(L, "cannot specify both the args option and array values");
			spawn_param_args(params);           /* cmd opts ... */
			break;
		}
		lua_getfield(L, 2, "env");              /* cmd opts ... envtab */
		switch (lua_type(L, -1)) {
		default: return luaL_error(L, "env option must be a table");
		case LUA_TNIL:
		case LUA_TTABLE:
			spawn_param_env(params);            /* cmd opts ... */
			break;
		}
		get_redirect(L, 2, "stdin", params);    /* cmd opts ... */
		get_redirect(L, 2, "stdout", params);   /* cmd opts ... */
		get_redirect(L, 2, "stderr", params);   /* cmd opts ... */
		break;
	}
	return spawn_param_execute(params);         /* proc/nil error */
}


static const luaL_reg ex_iolib[] = {
	{"pipe",       ex_pipe},
	{0,0}
};
static const luaL_reg ex_iofile_methods[] = {
	{"lock",       ex_lock},
	{"unlock",     ex_unlock},
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
static const luaL_reg ex_diriter_methods[] = {
	{"__gc",       diriter_close},
/*	{"__tostring", diriter_tostring}, */
	{0,0}
};
static const luaL_reg ex_process_methods[] = {
	{"__tostring", process_tostring},
	{"wait",       process_wait},
	{0,0}
};

/* copy the fields given in 'l' from one table to another; insert missing fields */
static void copy_fields(lua_State *L, const luaL_reg *l, int from, int to)
{
	from = absindex(L, from);
	to = absindex(L, to);
	for (; l->name; l++) {
		lua_getfield(L, from, l->name);
		if (lua_isnil(L, -1)) {
			lua_pop(L, 1);
			lua_pushcfunction(L, l->func);
		}
		lua_setfield(L, to, l->name);
	}
}

int luaopen_ex(lua_State *L)
{
	/* Make all functions available via ex. namespace */
	luaL_register(L, "ex", ex_iolib);           /* . ex */
	luaL_register(L, 0, ex_oslib);
	luaL_register(L, 0, ex_iofile_methods);
	luaL_register(L, 0, ex_process_methods + 1); /* XXX don't insert __tostring */
	lua_replace(L, 1);                          /* ex . */

	/* extend the os table */
	lua_getglobal(L, "os");                     /* ex . os */
	if (lua_isnil(L, -1)) return luaL_error(L, "os not loaded");
	copy_fields(L, ex_oslib, 1, -1);            /* ex . os */

	/* extend the io table */
	lua_getglobal(L, "io");                     /* ex . io */
	if (lua_isnil(L, -1)) return luaL_error(L, "io not loaded");
	copy_fields(L, ex_iolib, 1, -1);            /* ex . io */
	lua_getfield(L, 1, "pipe");                 /* ex . io ex_pipe */
	lua_getfield(L, -2, "stderr");              /* ex . io ex_pipe io_stderr */
	lua_getfenv(L, -1);                         /* ex . io ex_pipe io_stderr E */
	lua_setfenv(L, -3);                         /* ex . io ex_pipe io_stderr */

	/* extend the io.file metatable */
	luaL_getmetatable(L, LUA_FILEHANDLE);       /* ex . F  */
	if (lua_isnil(L, -1)) return luaL_error(L, "can't find FILE* metatable");
	copy_fields(L, ex_iofile_methods, 1, -1);   /* ex . F */

	/* diriter metatable */
	luaL_newmetatable(L, DIR_HANDLE);           /* ex . D */
	luaL_register(L, 0, ex_diriter_methods);    /* ex . D */

	/* proc metatable */
	luaL_newmetatable(L, PROCESS_HANDLE);       /* ex . P */
	copy_fields(L, ex_process_methods, 1, -1);  /* ex . P */
	lua_pushliteral(L, "__index");              /* ex . P __index */
	lua_pushvalue(L, -2);                       /* ex . P __index P */
	lua_settable(L, -3);                        /* ex . P */

	lua_settop(L, 1);                           /* ex */
	return 1;
}
