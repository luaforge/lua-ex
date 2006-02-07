#include <stdlib.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <windows.h>

#include "spawn.h"

extern void *checkuserdata(lua_State *L, int index, const char *name);
extern HANDLE get_handle(FILE *f);
extern int push_error(lua_State *L);

static int needs_quoting(const char *s)
{
	return s[0] != '"' && strchr(s, ' ');
}

/* filename ... */
void spawn_param_filename(struct spawn_params *p)
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
void spawn_param_defaults(struct spawn_params *p)
{
	p->environment = 0;
	memset(&p->si, 0, sizeof p->si);
	p->si.cb = sizeof p->si;
}

/* cmd opts ... argtab -- cmd opts ... cmdline */
void spawn_param_args(struct spawn_params *p)
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
void spawn_param_env(struct spawn_params *p)
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
		FILE **pf = checkuserdata(p->L, -1, LUA_FILEHANDLE);
		*ph = get_handle(*pf);
	}
	lua_pop(p->L, 1);
	return ret;
}

/* _ opts ... */
void spawn_param_redirects(struct spawn_params *p)
{
	p->si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
	p->si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
	p->si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);
	if (get_redirect(p, "stdin",  &p->si.hStdInput)
		| get_redirect(p, "stdout", &p->si.hStdOutput)
		| get_redirect(p, "stderr", &p->si.hStdError))
		p->si.dwFlags = STARTF_USESTDHANDLES;
}

int spawn_param_execute(struct spawn_params *p, struct process *proc)
{
	char *c = strdup(p->cmdline);
	char *e = (char *)p->environment; // strdup(p->environment);
	PROCESS_INFORMATION pi;
	proc->status = -1;
	/* XXX does CreateProcess modify its environment argument? */
	int ret = CreateProcess(0, c, 0, 0, 0, 0, e, 0, &p->si, &pi);
	if (e) free(e);
	free(c);
	if (ret) {
		proc->hProcess = pi.hProcess;
		proc->dwProcessId = pi.dwProcessId;
	}
	return ret;
}

/* proc -- exitcode/nil error */
int process_wait(lua_State *L)
{
	struct process *p = checkuserdata(L, 1, PROCESS_HANDLE);
	if (p->status == -1) {
		DWORD exitcode;
		if (WAIT_FAILED == WaitForSingleObject(p->hProcess, INFINITE)
		    || !GetExitCodeProcess(p->hProcess, &exitcode))
			return push_error(L);
		p->status = exitcode;
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
		sprintf(buf, "process (%lu, %s)", (unsigned long)p->dwProcessId,
			p->status==-1 ? "running" : "terminated"));
	return 1;
}
