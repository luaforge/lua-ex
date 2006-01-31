#ifndef SPAWN_H
#define SPAWN_H

#ifdef MISSING_POSIX_SPAWN
#include "posix_spawn.h"
#endif

#include <lua.h>
#include <sys/types.h>

#define PROCESS_HANDLE "process"
struct process {
	int status;
	pid_t pid;
};

struct spawn_params {
	lua_State *L;
	const char *command, **argv, **envp;
	posix_spawn_file_actions_t file_actions;
	int has_actions;
};

void spawn_param_filename(struct spawn_params *p);
void spawn_param_defaults(struct spawn_params *p);
void spawn_param_args(struct spawn_params *p);
void spawn_param_env(struct spawn_params *p);
void spawn_param_redirects(struct spawn_params *p);
int spawn_param_execute(struct spawn_params *p, struct process *proc);
int process_wait(lua_State *L);

#endif/*SPAWN_H*/
