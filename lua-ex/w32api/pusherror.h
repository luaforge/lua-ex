#ifndef pusherror_h
#define pusherror_h

#include <lua.h>
#include <windows.h>

int windows_pusherror(lua_State *L, DWORD error, int nresults);
#define windows_pushlasterror(L) windows_pusherror(L, GetLastError(), -2)

#endif/*pusherror_h*/
