CFLAGS = $(WARNINGS) $(DEFINES) $(INCLUDES)
DEFINES = $(DEBUG) -D_XOPEN_SOURCE=500 -DMISSING_POSIX_SPAWN
DEBUG= -D'debug(...)=fprintf(stderr,__VA_ARGS__)'
# -D_POSIX_SOURCE -D_XOPEN_SOURCE=600 -D_POSIX_C_SOURCE=200112L \
	-U__STRICT_ANSI__ -D_GNU_SOURCE
INCLUDES = -I$(LUA)/include
WARNINGS = -W -Wall
#LUA = /home/mark/src/lang/lua/lua-5.1-rc2
LUA = /home/mark/src/lang/lua/lua51

ex-OBJS = ex.o spawn.o

default: ex.dll

ex.so: $(ex-OBJS); $(CC) -shared -o $@ $(ex-OBJS)

#LIBS = -L$(LUA)/lib -llua51 spawn.a
EXTRA = posix_spawn.o
ex.dll: $(ex-OBJS) $(EXTRA) $(LIBS); $(CC) -shared -L$(LUA)/bin/Cygwin -o $@ $(ex-OBJS) $(EXTRA) -llua51

ex.o: ex.c spawn.h
spawn.o: spawn.c spawn.h

posix_spawn.o: posix_spawn.c posix_spawn.h

clean:; rm -f *.o ex.dll ex.so ex.a
