#ifndef SPAWN_H
#define SPAWN_H
#include <lua.h>

#include <windows.h>

struct spawn_params {
	lua_State *L;
	const char *cmdline;
	const char *environment;
	STARTUPINFO si;
};

#define PROCESS_HANDLE "process"
struct process {
    int status;
    HANDLE hProcess;
};

void spawn_param_filename(struct spawn_params *p);
void spawn_param_defaults(struct spawn_params *p);
void spawn_param_args(struct spawn_params *p);
void spawn_param_env(struct spawn_params *p);
void spawn_param_redirects(struct spawn_params *p);
int spawn_param_execute(struct spawn_params *p, struct process *proc);

int process_wait(lua_State *L);

#endif/*SPAWN_H*/
