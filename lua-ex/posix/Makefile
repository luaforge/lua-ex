CFLAGS = $(WARNINGS) $(DEFINES) $(INCLUDES)
DEFINES = $(DEBUG) -D_XOPEN_SOURCE=600 -DMISSING_POSIX_SPAWN -DMISSING_ENVIRON_DECL="extern char **environ"
DEBUG= -D'debug(...)=fprintf(stderr,__VA_ARGS__)'
# -D_POSIX_SOURCE -D_XOPEN_SOURCE=600 -D_POSIX_C_SOURCE=200112L \
	-U__STRICT_ANSI__ -D_GNU_SOURCE
INCLUDES = -I$(LUA)/include -I$(LUA)/src
WARNINGS = -W -Wall
#LUA = /home/mark/src/lang/lua/lua-5.1-rc2
#LUA = /home/mark/src/lang/lua/lua51
LUA = /home/medgar/src/lang/lua/lua-5.1.1
LIBS = -L$(LUA)/src -llua5.1

ex-OBJS = ex.o spawn.o

default: ex.so

EXTRA = posix_spawn.o
ex.so: $(ex-OBJS) $(EXTRA); $(CC) -shared -o $@ $(ex-OBJS) $(EXTRA) $(LIBS)

ex.o: ex.c spawn.h
spawn.o: spawn.c spawn.h

posix_spawn.o: posix_spawn.c posix_spawn.h

clean:; rm -f *.o ex.dll ex.so ex.a
