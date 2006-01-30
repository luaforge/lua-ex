CFLAGS = -std=c99 $(WARNINGS) $(DEFINES) $(INCLUDES)
DEFINES = -D_POSIX_SOURCE -D_XOPEN_SOURCE=600 -D_POSIX_C_SOURCE=200112L -U__STRICT_ANSI__
INCLUDES = -I$(LUA)/include -I.
WARNINGS = -W -Wall
#LUA = /home/mark/src/lang/lua/lua-5.1-rc2
LUA = /home/mark/src/lang/lua/lua51

ex.so: ex.o; $(CC) -shared -o $@ ex.o

#LIBS = -L$(LUA)/lib -llua51 spawn.a
ex.dll: ex.o $(LIBS); $(CC) -shared -L$(LUA)/bin/Cygwin -o $@ ex.o spawn.o -llua51

spawn.o: spawn.c spawn.h
