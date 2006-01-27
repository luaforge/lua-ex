TARGET_ARCH = -mno-cygwin
CFLAGS = $(WARNINGS) $(DEFINES) $(INCLUDES)
DEFINES = -DWIN32_LEAN_AND_MEAN
INCLUDES = -I${LUA}/include
WARNINGS = -W -Wall
LUA = /home/mark/src/lang/lua/lua-5.1-rc2
LUALIBS = -L$(LUA)/lib -llua51

ex.dll: ex.o
	$(CC) $(TARGET_ARCH) -shared -o $@ ex.o $(LUALIBS) /c/windows/system32/msvcrt.dll
