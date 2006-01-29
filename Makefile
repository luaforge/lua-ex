CFLAGS = $(WARNINGS) $(DEFINES) $(INCLUDES)
DEFINES = -D_XOPEN_SOURCE=600
INCLUDES = -I${LUA}/src
WARNINGS = -W -Wall
LUA = /home/mark/src/lang/lua/lua-5.1-rc1

lu.so: lu.o; $(CC) -shared -o $@ lu.o
