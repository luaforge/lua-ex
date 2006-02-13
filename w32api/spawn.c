#include <stdlib.h>

#include <lua.h>
#include <lauxlib.h>

#include <windows.h>

#include "spawn.h"

#define debug(...) /* fprintf(stderr, __VA_ARGS__) */
#define debug_stack(L) /* #include "../lds.c" */

extern int push_error(lua_State *L);
extern HANDLE get_handle(FILE *f);

static int needs_quoting(const char *s)
{
	return s[0] != '"' && strchr(s, ' ');
}

struct spawn_params {
	lua_State *L;
	const char *cmdline;
	const char *environment;
	STARTUPINFO si;
};

struct spawn_params *spawn_param_init(lua_State *L)
{
	static const STARTUPINFO si = {sizeof si};
	struct spawn_params *p = lua_newuserdata(L, sizeof *p);
	p->L = L;
	p->cmdline = p->environment = 0;
	p->si = si;
	return p;
}

void spawn_param_filename(struct spawn_params *p, const char *filename)
{
	p->cmdline = filename;
	if (needs_quoting(p->cmdline)) {
		lua_pushliteral(p->L, "\"");       /* cmd ... q */
		lua_pushstring(p->L, p->cmdline);  /* cmd ... q cmd */
		lua_pushvalue(p->L, -2);           /* cmd ... q cmd q */
		lua_concat(p->L, 3);               /* cmd ... "cmd" */
		p->cmdline = lua_tostring(p->L, -1);
	}
}

/* cmd opts ... argtab -- cmd opts ... cmdline */
void spawn_param_args(struct spawn_params *p)
{
	lua_State *L = p->L;
	debug("spawn_param_args:"); debug_stack(L);
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
	lua_insert(L, -2);             /* cmd opts ... argtab cmd argstr */
	lua_concat(L, 2);              /* cmd opts ... argtab cmdline */
	lua_replace(L, -2);            /* cmd opts ... cmdline */
	p->cmdline = lua_tostring(L, -1);
}

/* ... envtab/nil */
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

void spawn_param_redirect(struct spawn_params *p, const char *stdname, FILE *f)
{
	HANDLE h = get_handle(f);
	SetHandleInformation(h, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
	if (!(p->si.dwFlags & STARTF_USESTDHANDLES)) {
		p->si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
		p->si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
		p->si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);
		p->si.dwFlags |= STARTF_USESTDHANDLES;
	}
	switch (stdname[3]) {
	case 'i': p->si.hStdInput = h; break;
	case 'o': p->si.hStdOutput = h; break;
	case 'e': p->si.hStdError = h; break;
	}
}

struct process {
	int status;
	HANDLE hProcess;
	DWORD dwProcessId;
};

int spawn_param_execute(struct spawn_params *p)
{
	lua_State *L = p->L;
	struct process *proc;
	char *c, *e;
	PROCESS_INFORMATION pi;
	BOOL ret;

	proc = lua_newuserdata(L, sizeof *proc);       /* cmd opts ... proc */
	luaL_getmetatable(L, PROCESS_HANDLE);          /* cmd opts ... proc M */
	lua_setmetatable(L, -2);                       /* cmd opts ... proc */
	proc->status = -1;
	c = strdup(p->cmdline);
	e = (char *)p->environment; /* strdup(p->environment); */
	/* XXX does CreateProcess modify its environment argument? */
	ret = CreateProcess(0, c, 0, 0, TRUE, 0, e, 0, &p->si, &pi);
	/* if (e) free(e); */
	free(c);
	if (!ret)
		return push_error(L);
	proc->hProcess = pi.hProcess;
	proc->dwProcessId = pi.dwProcessId;
	return 1;
}

/* proc -- exitcode/nil error */
int process_wait(lua_State *L)
{
	struct process *p = luaL_checkudata(L, 1, PROCESS_HANDLE);
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
	struct process *p = luaL_checkudata(L, 1, PROCESS_HANDLE);
	char buf[40];
	lua_pushlstring(L, buf,
		sprintf(buf, "process (%lu, %s)", (unsigned long)p->dwProcessId,
			p->status==-1 ? "running" : "terminated"));
	return 1;
}
