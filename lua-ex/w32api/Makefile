TARGET_ARCH = -mno-cygwin
CFLAGS = $(WARNINGS) $(DEFINES) $(INCLUDES)
DEFINES = -DWIN32_LEAN_AND_MEAN
INCLUDES = -I${LUA}/include
WARNINGS = -W -Wall -Wno-missing-braces
#LUA = /home/mark/src/lang/lua/lua-5.1-rc2
LUA = /home/mark/src/lang/lua/lua51
LUALIBS = -L$(LUA)/bin/mingw -llua51

ex.dll-OBJS = ex.o spawn.o
ex.dll: $(ex.dll-OBJS)
	$(CC) $(TARGET_ARCH) -shared -o $@ $(ex.dll-OBJS) $(LUALIBS) /c/windows/system32/msvcrt.dll
