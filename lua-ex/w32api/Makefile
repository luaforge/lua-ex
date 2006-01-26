TARGET_ARCH = -mno-cygwin
CFLAGS = $(WARNINGS) $(DEFINES) $(INCLUDES)
DEFINES = -DWIN32_LEAN_AND_MEAN
INCLUDES = -I${LUA}/src
WARNINGS = -W -Wall
LUA = /home/mark/src/lang/lua/lua-5.1
LUALIBS = -L$(LUA)/src -llua51

osex.dll: osex.o
	$(CC) -shared -o $@ osex.o $(LUALIBS) /c/windows/system32/msvcrt.dll
