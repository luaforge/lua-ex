CFLAGS = -std=c99 $(WARNINGS) $(DEFINES) $(INCLUDES)
DEFINES = -D_POSIX_SOURCE -D_XOPEN_SOURCE=600 -D_POSIX_C_SOURCE=200112L -U__STRICT_ANSI__
INCLUDES = -I${LUA}/src
WARNINGS = -W -Wall
LUA = /home/mark/src/lang/lua/lua-5.1-rc2

ex.so: ex.o; $(CC) -shared -o $@ ex.o

#LIBS = -L$(LUA)/lib -llua51 spawn.a
ex.dll: ex.o $(LIBS); $(CC) -shared -o $@ ex.o $(LUALIBS) $(LIBS)

spawn.a: spawn.o; $(AR) r $@ $<
	
