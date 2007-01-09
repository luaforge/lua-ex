TARGET_ARCH = -mno-cygwin
CFLAGS = $(WARNINGS) $(DEFINES) $(INCLUDES)
DEFINES = -DWIN32_LEAN_AND_MEAN
INCLUDES = -I${LUA}/include
WARNINGS = -W -Wall -Wno-missing-braces
#LUA = /home/mark/src/lang/lua/lua-5.1-rc2
#LUA = /home/mark/src/lang/lua/lua51
LUA = /home/medgar/work/noom/lua/lua51
LUALIBS = -L$(LUA) -llua5.1

ex.dll-OBJS = ex.o spawn.o pusherror.o dirent.o
ex.dll-LIBS = $(LUALIBS) $(EXTRALIBS)
ex.dll: $(ex.dll-OBJS)
	$(CC) $(TARGET_ARCH) -shared -o $@ $(ex.dll-OBJS) $(ex.dll-LIBS)
clean:; rm ex.dll *.o

wdir.o: wdir.c wdir.h
