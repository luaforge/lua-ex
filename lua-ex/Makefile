CFLAGS = $(WARNINGS) $(DEFINES) $(INCLUDES)
WARNINGS = -W -Wall
#DEFINES = -D_XOPEN_SOURCE=600
INCLUDES = -I$(LUA)/include

LUA = /home/mark/src/lang/lua/lua-5.1-rc1

default:; echo Choose platform: mingw cygwin posix
all: mingw cygwin

mingw:; $(MAKE) -C w32api ex.dll
cygwin:; $(MAKE) -C posix ex.dll
posix:; $(MAKE) -C posix ex.so

#"EX_LIB=ex.so" "DEFINES=-D_XOPEN_SOURCE=600"
