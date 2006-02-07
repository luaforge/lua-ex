#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <unistd.h>
#include <sys/wait.h>

#include "spawn.h"

extern int push_error(lua_State *L);
extern void *checkuserdata(lua_State *L, int index, const char *name);

/* filename ... */
void spawn_param_filename(struct spawn_params *p)
{
	/* XXX confusing */
	p->command = luaL_checkstring(p->L, 1);
}

/* -- */
void spawn_param_defaults(struct spawn_params *p)
{
	p->argv = lua_newuserdata(p->L, 2 * sizeof *p->argv);
	p->argv[0] = p->command;
	p->argv[1] = 0;
	p->envp = (const char **)environ;
	p->has_actions = 0;
}

/* Converts a Lua array of strings to a null-terminated array of char pointers.
 * Pops a (0-based) Lua array and replaces it with a userdatum which is the
 * null-terminated C array of char pointers.  The elements of this array point
 * to the strings in the Lua array.  These strings should be associated with
 * this userdatum via a weak table for GC purposes, but they are not here.
 * Therefore, any function which calls this must make sure that these strings
 * remain available until the userdatum is thrown away.
 */
/* ... array -- ... vector */
static const char **make_vector(lua_State *L)
{
	size_t i, n = lua_objlen(L, -1);
	const char **vec = lua_newuserdata(L, (n + 2) * sizeof *vec);
	                                   /* ... arr vec */
	for (i = 0; i <= n; i++) {
		lua_rawgeti(L, -2, i);         /* ... arr vec elem */
		vec[i] = lua_tostring(L, -1);
		lua_pop(L, 1);                 /* ... arr vec */
	}
	vec[n + 1] = 0;
	lua_replace(L, -2);                /* ... vector */
	return vec;
}

/* ... argtab */
void spawn_param_args(struct spawn_params *p)
{
	const char **argv = make_vector(p->L);
	if (!argv[0]) argv[0] = p->command;
	p->argv = argv;
}

/* ... envtab/nil */
void spawn_param_env(struct spawn_params *p)
{
	size_t i = 0;
	luaL_Buffer estr;
	if (lua_isnil(p->L, -1)) {
		p->envp = (const char **)environ;
		return;
	}
	luaL_buffinit(p->L, &estr);
	lua_newtable(p->L);                    /* ... envtab arr */
	lua_pushnil(p->L);                     /* ... envtab arr nil */
	for (i = 0; lua_next(p->L, -3); i++) { /* ... envtab arr k v */
		luaL_prepbuffer(&estr);
		lua_pushvalue(p->L, -2);           /* ... envtab arr k v k */
		luaL_addvalue(&estr);
		luaL_putchar(&estr, '=');
		lua_pop(p->L, 1);                  /* ... envtab arr k v */
		luaL_addvalue(&estr);
		lua_pop(p->L, 1);                  /* ... envtab arr k */
		luaL_pushresult(&estr);            /* ... envtab arr k estr */
		lua_rawseti(p->L, -3, i);          /* ... envtab arr[n]=estr k */
	}                                      /* ... envtab arr */
	lua_replace(p->L, -2);                 /* ... arr */
	make_vector(p->L);                     /* ... arr */
}

/* _ opts ... */
static int get_redirect(struct spawn_params *p, const char *stdname, int descriptor)
{
	int ret;
	lua_getfield(p->L, 2, stdname);
	if ((ret = !lua_isnil(p->L, -1)))
		/* XXX checkuserdata is confusing here */
		posix_spawn_file_actions_adddup2(&p->file_actions, 
			fileno(checkuserdata(p->L, -1, LUA_FILEHANDLE)), descriptor);
	lua_pop(p->L, 1);
	return ret;
}

/* _ opts ... */
void spawn_param_redirects(struct spawn_params *p)
{
	posix_spawn_file_actions_init(&p->file_actions);
	p->has_actions = 1;
	get_redirect(p, "stdin", STDIN_FILENO);
	get_redirect(p, "stdout", STDOUT_FILENO);
	get_redirect(p, "stderr", STDERR_FILENO);
}

int spawn_param_execute(struct spawn_params *p, struct process *proc)
{
	int ret;
	proc->status = -1;
	ret = posix_spawnp(&proc->pid, p->command, &p->file_actions, 0, (char *const *)p->argv, (char *const *)p->envp);
	if (p->has_actions)
		posix_spawn_file_actions_destroy(&p->file_actions);
	return ret == 0;
}


/* proc -- exitcode/nil error */
int process_wait(lua_State *L)
{
	struct process *p = checkuserdata(L, 1, PROCESS_HANDLE);
	if (p->status == -1) {
		int status;
		if (-1 == waitpid(p->pid, &status, 0))
			return push_error(L);
		p->status = WEXITSTATUS(status);
	}
	lua_pushnumber(L, p->status);
	return 1;
}

/* proc -- string */
int process_tostring(lua_State *L)
{
	struct process *p = checkuserdata(L, 1, PROCESS_HANDLE);
	char buf[40];
	lua_pushlstring(L, buf,
		sprintf(buf, "process (%lu, %s)", (unsigned long)p->pid,
			p->status==-1 ? "running" : "terminated"));
	return 1;
}
